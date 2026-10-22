# Op 驱动的 OMS 事务模型设计文档

- 状态: 已评审, 待实施
- 日期: 2026-10-18
- 评审方式: 四路对抗评审(正确性对抗 / 落地规划 / 面试官视角 / 镀金审查), 本文档吸收全部评审结论
- 适用范围: src/oms, src/td, src/strategy; 不涉及 md 行情热路径

---

## 1. 背景与动机

### 1.1 现状

OMS(`src/oms/order_manager.h/.cc`)目前是被动簿记: journal 按 order_ref 存最新 OrderReport(到达即覆盖), 仓位账本按今/昨桶累计, 快照整文件落盘恢复。订单生命周期 FSM 是隐式的——broker 回报流是唯一事实源, 但没有转移校验, 没有幂等保护, 没有在途单概念。

### 1.2 已确认的现网缺陷(本设计的动机, 均有代码定位)

1. **成交重放双计(生死线)。** `OrderManager::OnTdTrade` → `ApplyTrade` 没有 trade_id 去重(`src/oms/order_manager.cc`), TradeReport 的 `trade_id` 字段(`src/td/order.h:78`)无人消费。重连或进程重启后私有流重放, 同一笔成交重复记账: 仓位量可由下次持仓查询的 broker-wins 纠正, 但 `avg_price`/`realized_pnl` 是本地独有的(broker 快照不带), 一旦翻倍无法修复。
2. **状态映射自相矛盾, 策略可永久卡死。** `StatusFromCtp` 把 `PartTradedNotQueueing`/`NoTradeNotQueueing` 映射为 `kSubmitted`(活单, `src/td/detail/order_ctp.cc:58-59`), 而撤单键表的 `IsAliveStatus` 把它们当终态(`src/td/detail/td_spi_adapter.cc:29-30`)。后果: 撤单键已删、OMS 认为单还活着、策略只认三个终态, `order_in_flight_` 永不清除, 策略再也不下单。触发源: FAK/IOC 残余撤销、broker 风控强平、RESUME 收养的手工单。
3. **在途单盲区。** 风控闸(`src/oms/checked_order_sink.cc`)的仓位与保证金检查只读账本快照, working order 完全不可见。在途开仓单不占额度, 快速连续信号可突破仓位上限。
4. **回报乱序/倒退无护栏。** journal 是 arrival-LWW(最新到达覆盖), 重放旧回报与新回报交错时状态可倒退(终态被旧回报复活)。
5. **撤单超时是策略私货。** `order_timeout_ms`/`cancel_sent_`/`order_in_flight_` 手写在 SMA 策略里, 且超时只在 OnTick 里求值——行情停摆时超时撤单不发生。

### 1.3 设计动机一句话

把订单生命周期建模为 op 流, 状态 = op 的纯函数折叠; 借用 CRDT 的代数性质(幂等/单调/终态吸收/跨实体交换)作为可机器验证的不变式, 让乱序、重放、对账从"靠经验堆防御代码"变成"性质测试门禁"。

**词汇纪律: 本设计借用 CRDT 的代数性质做单机簿记的正确性论证。禁止对外声称"实现了 CRDT"——没有多副本、没有并发写、没有网络分区, 准确说法是 op 集在每个实体上构成 join-semilattice 的单子集。**

---

## 2. 设计总览

### 2.1 Transaction 三步

```
Transaction = 一条权威事件 或 一条本地 intent
  ① 校验:  会话阶梯准入(ready?) + 风控闸(强一致) + 格序合法性(合法膨胀?)
  ② fold:  纯函数 (state, op) → state'          — 唯一写状态的步骤
  ③ 派生:  (state, state') → effects[]          — 通知策略/落盘/告警, 不进格
```

- 全序由 td-dispatch 单 sequencer 保证(现有线程模型不变); op 不需要因果元数据。
- fold 纯函数化是全部可测性的来源; effects 列表化是全部副作用的出口。
- effects 内部先实现为现有 TdClient 广播扇出, 逐 client 收缩迁移; **广播是输入通道, effects 是输出通道, 两者不在同一层, 不存在谁替换谁**。

### 2.2 op 化的边界(写操作全集, 不多不少)

- **是 op**: 一切改写 OMS 状态的动作——下单、撤单、超时、成交、状态回报、对账收养、日切翻滚。
- **不是 op**:
  - 查询(读操作; 查询应答里携带的行才是 Adopt op);
  - tick(行情是流不是事务, 且是延迟敏感路径, 不适用 op 信封);
  - 会话阶梯事件(它是准入闸门, 决定哪些 intent 允许生成, 与 op 流分层)。

---

## 3. Op 模型

### 3.1 op 两类(评审后从三类砍为两类)

| 类别 | origin | 语义 |
|---|---|---|
| 权威 op | broker 回报/查询行 | 直接改写格状态 |
| 本地 intent op | 策略/控制台/OMS 定时器 | 乐观标记, 被权威 op confirm 才升格, 被 discard 即蒸发; **intent 不进格** |

通知类(Reject/CancelReject/Error)不单列: 它是"消费一个 intent + 空状态变更 + effects"的权威 op 特例。

### 3.2 op 清单

权威 op:
- `SubmitAck{order_ref, order_sys_id}` — confirm Submit intent
- `Fill{trade_id, qty, price}` — 成交
- `CancelConfirm{order_ref}` — 撤单成功
- `Adopt{report}` — 查询行/重连收养(现有 UpsertCancelKey 的形式化)
- `Expire{order_ref}` — 日终过期, **由对账合成**(昨日非终态单在次日查询中缺席 ⇒ 合成 Expire; CTP 对 GFD 日终过期不一定推回报)

本地 intent op:
- `Submit{req}` — 下单意图
- `CancelReq{order_ref}` — 撤单意图
- `TimeoutCancel{order_ref}` — 超时撤单(OMS 定时器驱动, 取代策略私货; 顺带修复"行情停摆则超时不触发"的隐患)

### 3.3 op 信封与幂等键

```
Op = { writer_id, seqno, key, payload }
```

- `writer_id`: 写入者标识, 现在恒为 0。**为多进程预留的唯一字段**——op 身份一旦落盘不可追溯修改, 此字段现在不加, 将来加就是格式灾难。
- `seqno`: per-writer 单调序号。现有用途: 检测私有流重放接缝的缺口; 未来用途: 分布式合并的前提。
- 幂等键:
  - 成交: `(exchange_id, trading_day, trade_id)` —— TradeID 只在同交易所同交易日内唯一, 跨交易所会撞;
  - 订单: `(trading_day, order_ref)` —— MaxOrderRef 每日归零, 裸 order_ref 跨日重启撞键。journal 键同步加 trading_day 维度, 或日切时归档 journal(现 `RollTradingDay` 只滚仓位桶不碰 journal)。
- seen-set(seen trade_id)必须随快照/op-log 持久化, 否则进程重启后重放照样双计。

### 3.4 intent 终判集合

每个 intent 必然且只会终结于四种之一:

1. 权威 confirm(SubmitAck / CancelConfirm);
2. 权威 reject(OnRspOrderInsert / OnErrRtnOrderInsert / OnRspOrderAction / OnErrRtnOrderAction)——**所有拒绝回调必须把可配对引用透传进来**; 现代码 `OnRspOrderAction`/`OnErrRtnOrderAction` 丢弃了 req/action, 需补;
3. **订单终态吸收**——撤单-成交竞速中 CancelConfirm 可能永远不来(对方已成交, 只有 `OnRtnOrder(kAllTraded)`), 订单进入终态即视为 CancelReq 被 discard;
4. **重连快照缺席**——请求在断线中蒸发(rc=0 但未达 broker)的唯一终判来源: 重连对账时快照缺席且未 confirm 的 intent 一律 discard。

`req==nullptr` 的拒绝分支(CTP 会发生)不产生任何报告: 走 fail-safe——冻结该 intent 关联额度 + 告警, 等待第 4 条终判。

---

## 4. 状态模型与 fold

### 4.1 OrderEntry 分量语义

| 分量 | 语义 | 规则 |
|---|---|---|
| volume_traded | 单调只增(G-Counter) | `max(报告值, Σfills)`——撤单-成交竞速下订单流与成交流瞬时分歧, 两个权威源取 max 保持单调 |
| status | 单调 FSM | kSubmitted < kPartTraded < {kAllTraded, kCanceled, kRejected}; 终态吸收, 倒退=拒绝+warn |
| updated_seq | sequencer 序号 | **不用本地墙钟**(重启+NTP 回拨即破偏序) |
| cancel_key | 不属于 OMS 格 | td_spi_adapter 的撤单键表已有职责, 不重复收编 |

状态映射修正(切片 0): `PartTradedNotQueueing`/`NoTradeNotQueueing` 映到 `kCanceled`(终态允许 volume_traded>0), FSM 终态集合与 `IsAliveStatus` 对齐。

`kUnknown`(条件单 NotTouched/Touched 映射而来)作为 ⊥ 旁支: 只告警, 不进 FSM。本系统自发单 `ContingentCondition=Immediately` 不产出该状态, 但 RESUME 可收养同账户手工条件单。

### 4.2 非格分量(如实标注, 不硬塞进格框架)

- **账户**: LWW, broker 快照整体覆盖。权益双向波动, 不膨胀。
- **仓位**: 按交易日划 epoch。日切今/昨桶翻滚是非单调重置, 格只在 epoch 内成立; 日切 = 新 epoch, 旧态归档进 op-log。
- **avg_price / realized_pnl**: 不单调且 broker 快照不带。归属待拍板(见 §10): op-log 全量重放 derive(要求 log 不丢)或快照本地优先字段(现状即如此, 便宜)。**倾向后者**。
- **intent**: 不进格, 独立注册表, confirm 才升格。

---

## 5. 不变式体系

### 5.1 代数律(收敛性)

- 幂等律: `apply(s, op) == apply(apply(s, op), op)` —— 重连重放无害;
- 终态吸收律: 终态 ⊔ 任意 op == 终态 —— 乱序旧回报不能复活死单;
- 跨实体交换律: 不同 order_ref/合约的 op 任意顺序 fold 结果一致;
- 单调律: volume_traded 不倒退, status 不倒退。

### 5.2 业务守恒律(期货规则, 与代数律共用同一套性质测试装置)

- 每订单量守恒: `volume_traded + volume_total ≡ volume_total_original`;
- 今昨桶守恒: CloseToday 成交 ⇒ today 桶恰好减同量; 任何桶不为负;
- 拆腿守恒: PlanClose 拆出各腿 volume 之和 ≡ 请求量;
- 资金守恒(限定): fold 出的 Σ(fill价×量)±手续费 与权益变化一致(账户是 LWW, 只对 fold 部分验)。

### 5.3 性质测试纪律

- **必须先对旧实现跑红一次**: 重放双计、状态倒退各至少一条红灯记录, 这是"不变式有牙齿"的唯一证据。先写测试跑红, 再落 fold 修绿。
- 随机 op 序列 fuzz(从编辑器 fuzz transaction 平移): 所有排列 fold 结果一致 + 全部不变式恒真。
- intent 泄漏必须做成格律测试: 任意 op 序列下, 终结的 intent 必然离开注册表——一个漏清的 intent 会永久锁死该合约开仓。

---

## 6. 恢复与对账(三个致命洞的裁决规则, 切片 6 动工前必须全部落地为代码)

### 6.1 终态 vs 终态冲突裁决

CTP 查询流与私有流**无共享序号**, `OnRspQryOrder` 行与 RESUME 重放流交错进入同一通道, 无法判定谁新。裁决规则(不优雅但可判定):

- ready 之前(启动阶梯对账): snapshot-wins 整体覆盖 + 冲突告警;
- ready 之后: 只认私有流; 周期对账只告警不 join。

### 6.2 quiesce 闸门(仓位/账户快照)

- join-max 错: 宕机期间被强平, 本地>broker 是合法状态, 取 max 永久错;
- 裸 broker-wins 也错: 快照采样早于在途成交时, 后到快照会倒退账本(策略层已知此坑并 wait it out, OMS 层没有);
- `PositionRow` 无采样时刻, cut 不可知, 无法精确重放。

裁决: **无在途单(含 intent)才接受快照, 否则推迟到 quiesce**; 快照时刻之后的分歧只告警。账户 LWW 同理需闸门。

### 6.3 RESUME/RESTART 语义核实(已核实, 2026-10-22)

`src/td/td_gateway.cc` 订阅 `THOST_TERT_RESUME(=1)`: 断线后从 .con 书签续传, 断线窗口的回报不丢。RESTART(0) 全日重传, QUICK(2) 跳过断线窗口。原注释首行误写为 QUICK 语义, 已修正。结论: 崩溃若发生在书签刷盘之前, 接缝重复必然发生, trade_id 去重无条件必须做(已落地, 切片 2)。

### 6.4 裁决记录(2026-10-22 拍板, 切片 6 的开工前提)

- **终态冲突: 批准 6.1 原案。** 未 ready(启动/重连恢复窗口)查询流终态覆盖并告警; ready 后先到终态赢, 冲突只告警。实现: OrderReport 加来源标记(kStream/kQuery)。
- **quiesce 闸门: 批准 6.2 原案, 账户快照同闸门。** 无在途(intent 空 + 无活单 + 无挂起终态 effect)才应用快照, 否则保留最新一份推迟到 quiesce; 闸门期间分歧只告警。看门狗撤卡死单保证 quiesce 有界可达。
- **avg_price/realized_pnl: 快照本地优先。** broker 快照只裁决手数; 本地快照是这两个字段的唯一持久真相源(现状即正确, 固化为裁决); op-log 全量重放提供全精度 derive 作为审计路径。


### 6.5 撤单-成交竞速的 effects 时机

格层面无事(终态吸收 + volume 取 max), 但 effects 会抢跑: 策略看到 kCanceled 即认为 flight 结束, 尾随成交随后才改仓位 → 瞬间超仓。裁决: **"flight 结束"effect 延迟到 Σfills 追上终态报告的 volume_traded 才发出**。(CTP 常态就是全成回报先于成交明细到达, `order_manager.cc` 注释自述。)

---

## 7. 准入与风控(红线)

**格只用于"事后收敛的簿记", 一切"事前准入"不走格。** 最终一致性风控是语义矛盾: 分区期间各进程各自放行, 合并后超限。风控闸(仓位/保证金/时段)在下单路径上同步检查, 读含 intent 的悲观视图(在途单计入占用, 宁可误拒不可误放), intent 被拒/终结后释放占用。

---

## 8. 与会话阶梯的关系

两者正交: 阶梯管连通/就绪(td 七级, md 四级), op 管订单/仓位状态。唯一交点: Transaction 第①步的"阶梯准入"谓词, 切片 1-3 直接读现成的 `ready_` atomic 即可。两者共享: 同一个三步 Transaction 骨架、同一个性质测试套路、td-dispatch TaskLoop 定时器(重连退避/超时撤单共用)。实施顺序: op 先行, 阶梯抽取随时可插(它碰 CTP 边界代码, 风险更高, 可抄 op 的骨架)。

---

## 9. 分布式预留(预留选项, 不建机制)

多进程/分布式不实现, 但以下四个零成本决策现在做, 保留未来可能:

1. **op 信封 writer 维**(§3.3)——唯一"现在不加将来是灾难"的决策;
2. **跨实体不变量显式清单化**——分片可行性取决于哪些不变量跨实体; 答案现在很短: 只有风控闸。写进文档, 将来分片方案是显然的;
3. **fold 保持纯函数**——op 可跨进程搬运重演的全部前提, 本来就在设计里;
4. **守住队列形状的模块边界**——dispatcher 本来就是盒装转发(回调线程装箱→SPSC→消费线程), SPSC 换 IPC 时模块内部一行不改。

明确不预留: 向量时钟、合并协议、网络传输、共识。单 sequencer 下它们没有负载可背。

未来形态只有两种, 都与 op 模型兼容: 风控闸集中为单进程; 或按合约/账户分片, 每进程独占自己的准入域。

另注: §6 的三个裁决规则本质就是"如果明天变多进程今天必须回答的问题", 答好它们本身就是最实的分布式预留。

---

## 10. 落地切片计划

每切片独立可编译、独立测试、独立提交。提交信息遵守仓库惯例。

| 切片 | 内容 | 工作量 | 风险 |
|---|---|---|---|
| 0 | 状态映射对齐: NotQueueing→kCanceled, FSM 终态与 IsAliveStatus 对齐(独立 live bug) | 1-2h | 零 |
| 1 | 性质测试先写、**对旧实现跑红** → fold 纯函数+格守卫接入 OnTdOrder → 转绿 | 4-6h | 零(纯新增+委托) |
| 2 | trade_id 去重(键三元组, seen-set 持久化)+ 仓位 fold + 日切 op; avg_price 归属先拍板 | 4-6h | 低 |
| 3 | intent 注册表 + 风控计在途 + 拒绝回调透传配对引用 | 6-8h | 中(intent 泄漏须格律测试兜底) |
| 4 | TimeoutCancel 收归 OMS(TaskLoop 定时器)+ 删策略在途私货(~40 行)+ 策略测试重写 | 4-6h | 中(11 个策略测试大半重写) |
| 5 | effects 执行器(内部先跑旧 TdClient 扇出, client 逐个收缩, dispatcher 最后删) | 6-8h | 中(迁移期 FIFO 相对序不变量: 策略整体迁移不半迁) |
| 6 | op-log 落盘(帧格式+版本字节, 解析哲学从"严格"翻成"跳过未知"; 快照 v1 读取器保留)+ §6 裁决规则落地 + 恢复=fold(log)+§6.1/6.2 | 8-12h | 高 |

合计 32-46h。切片 1-3 是价值核(~14-20h), 独立成立。

关键纪律:
- op-log 只许写权威+intent op, 必须全 POD(与 `src/td/order.h:127-133` 的 static_assert 文化一致); effects 含 string 不落盘, 若哪天想让 effects 进 log 就是格式灾难的开始;
- order_ref 分配时序: intent 化后 ref 分配上移到校验通过之后, 但"先登记 intent 再发请求"必须保留(现 `td_spi_adapter.cc` 先注册撤单键再发请求, 因首个回报可能竞态先于返回), 否则 RESUME 收养的未知 ref 与本地 intent 对不上;
- 快照格式: CTSOMS v1 读取器保留(v1 快照 + broker 对账仍是合法恢复路径), 写侧升 v2。

## 11. 待拍板决策

1. **avg_price/realized_pnl 归属**(切片 2 前): op-log 全量重放 derive, 或快照本地优先字段。倾向后者。
2. **RESUME/RESTART 语义核实与注释修正**(§6.3, 随时可做)。

## 12. 验收标准(摘要)

- 切片 1: 性质测试存在对旧实现的红灯记录; fold 接入后全绿; 现有 213 测试零改动通过。
- 切片 2: 同一成交重放 N 次仓位/均价/PnL 不变; 跨日重启 journal 不串键。
- 切片 3: 在途开仓单计入风控占用; intent 泄漏格律测试通过。
- 切片 4: 行情停摆时超时撤单仍触发; 策略不再持有在途状态。
- 切片 5: effects 与原始事件 FIFO 相对序不变; TdDispatcher 最终可删。
- 切片 6: kill -9 后重启, 仓位/均价/PnL 与 broker 对账一致; 重放日志出现 "dup op, ignored"。
