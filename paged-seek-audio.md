# 分页播放（decoded_paged）连续拖动定位的读取行为分析

- 代码版本：miniaudio 0.11.26（`miniaudio.h`，96949 行）
- 分析对象：资源管理器的 **未知长度解码分页** 供给（`ma_resource_manager_data_supply_type_decoded_paged`）
- 配套可复算脚本：`research/sim/sim_paged_seek.py`（按源码逐函数等价改写，`python3 -I research/sim/sim_paged_seek.py` 可复跑本文全部数字）

---

## 1. 涉及的实际代码与线程模型

### 1.1 三个层次的对象

| 层次 | 类型 | 关键状态 | 访问线程 |
|---|---|---|---|
| 数据节点 | `ma_resource_manager_data_buffer_node`，`backend.decodedPaged`（`miniaudio.h:10438-10443`） | 页链表 `data`、`decodedFrameCount`、节点 `result`（`MA_BUSY`/`MA_SUCCESS`）、只属于作业线程的 `ma_decoder` | 作业线程写，播放线程原子读 |
| 连接器 | `ma_paged_audio_buffer`（`miniaudio.h:6006-6013`） | `pCurrent`、`relativeCursor`、`absoluteCursor` | 仅播放线程读/seek |
| 数据缓冲 | `ma_resource_manager_data_buffer`（`miniaudio.h:10461-10480`） | `seekTargetInPCMFrames`、`seekToCursorOnNextRead` | 仅播放线程（注释见 `miniaudio.h:10469`） |
| 声音 | `ma_sound` | 原子量 `seekTarget`（`miniaudio.h:11261`） | 任意线程写，混音线程消费 |

页结构是单链表（`miniaudio.h:5971-5985`）：一个 size 恒为 0 的哑头节点 `head`，原子指针 `pTail`，每页记录 `sizeInFrames` 和柔性数组 `pAudioData`。**链表只增不减**，头文件注释明确说明它为"异步流入、同时读取"设计，但只允许一个线程追加、一个线程读/seek（`miniaudio.h:5967-5969`）。

### 1.2 预取（解码）是一条自我续期的作业链

1. `LOAD_DATA_BUFFER_NODE` 作业对带 `UNKNOWN_LENGTH` 标志的声音初始化解码器与空的分页供给（`miniaudio.h:70744-70759`），随后投出第一个 `PAGE_DATA_BUFFER_NODE` 作业（`miniaudio.h:73133-73142`）。
2. 每个分页作业调用 `ma_resource_manager_data_buffer_node_decode_next_page()`（`miniaudio.h:70767`）：
   - 页大小固定为 `MA_RESOURCE_MANAGER_PAGE_SIZE_IN_MILLISECONDS(=1000) × sampleRate/1000`（`miniaudio.h:69529-69530`、`70779`），48 kHz 下即 **48000 帧/页**；
   - 先 `allocate_page`，再用**作业线程专属的解码器顺序向后解码**（`miniaudio.h:70820-70825`）；
   - 解码出帧才 `append_page` 挂到链表尾（`miniaudio.h:70829`，无锁 CAS 挂接，`miniaudio.h:60841-60861`），并累加 `decodedFrameCount`；
   - 一帧都读不到时释放空页并返回 `MA_AT_END`（`miniaudio.h:70837-70841`）。
3. 分页作业只要拿到 `MA_SUCCESS` 就再投一个同构作业（`miniaudio.h:73270-73281`），节点保持 `MA_BUSY`；拿到 `MA_AT_END` 时把它归一化成 `MA_SUCCESS`，再用 CAS 把节点 result 从 `MA_BUSY` 换掉（`miniaudio.h:73285-73296`）。

默认资源管理器只有 **1 个作业线程**（`miniaudio.h:70193`），且作业按 `executionPointer` 保序（`miniaudio.h:73253-73255`）。**数据缓冲这条链路没有任何"解码器跳转"作业**（seek 作业只存在于另一类对象 `ma_resource_manager_data_stream`），所以预取永远从解码器当前位置顺序向后跑，不会因为用户拖动而改变方向或速度。这就是"旧预取"与"新拖动"相遇的根源：拖动只动播放侧的连接器，解码侧的页链按自己的节奏继续生长。

### 1.3 播放侧的三个关键函数

**seek（`ma_paged_audio_buffer_seek_to_pcm_frame`，`miniaudio.h:61031-61077`）**

- 目标等于当前游标：直接成功，什么都不改；
- 目标在当前游标**之前**：先把 `pCurrent` 复位到哑头、两个游标清零，再落入前进逻辑；
- 前进逻辑只遍历**当前已经挂在链表上的页**，累计 `runningCursor` 找包含目标帧的页；
- 走到链表尾都找不到：`return MA_BAD_SEEK`，注释明确"不要改变任何状态"（`miniaudio.h:61072-61073`）；
- 特例：允许 seek 到尾页的末端（`frameIndex == pageRangeEnd && pPage == tail`，`miniaudio.h:61060`）。

**read（`ma_paged_audio_buffer_read_pcm_frames`，`miniaudio.h:60977-61029`）**

- 循环从 `pCurrent` 页的 `relativeCursor` 拷贝，跨页时原子加载 `pCurrent->pNext`；
- 当前页读完而 `pNext == NULL`：置 `MA_AT_END` 并 break——**注意此时 `totalFramesRead` 可能大于 0**，即"本页余粮已交付、下一页还没挂上"；
- 部分交付是常态：返回的 `framesRead` 是实际拷贝数，不保证等于请求数。

**数据缓冲包装层（`ma_resource_manager_data_buffer_read_pcm_frames`，`miniaudio.h:71635-71745`）**

- 入口先处理延迟 seek（`miniaudio.h:71666-71678`）：若连接器 seek 返回 `MA_BAD_SEEK` 且供给类型是 `decoded_paged`，**重新置位 `seekToCursorOnNextRead` 并返回 `MA_BUSY`，本次一帧不读**；这是全代码库中唯一对分页 `MA_BAD_SEEK` 做重试安排的地方（对照 `miniaudio.h:71671` 与全部 `MA_BAD_SEEK` 出现点）；
- 连接器 read 返回 `MA_AT_END` 时，只要节点仍是 `MA_BUSY`，就改写成 `MA_BUSY`（`miniaudio.h:71726-71730`）——**"等数据"和"播完了"在连接器里是同一个信号，靠节点 result 区分**；
- 分页供给不走 `decoded` 平铺缓冲那段 availableFrames 限流逻辑（`miniaudio.h:71684-71715` 只处理 `decoded` 类型），分页读多少完全由页链表现状决定。

### 1.4 声音层的两个"坑"

- `ma_sound_seek_to_pcm_frame()` **只做一次原子写就返回 `MA_SUCCESS`**（`miniaudio.h:79711-79713`），不触碰数据缓冲，更不检查目标帧是否已解码；
- 混音线程在处理回调里消费 `seekTarget` 时，**调用 seek 后丢弃返回码**（`miniaudio.h:77172-77180`），随即把 `seekTarget` 清成 `MA_SEEK_TARGET_NONE`。也就是说，经 `ma_sound` 发起、落到未解码区域的跳转，`MA_BAD_SEEK` 被静默吞掉，连接器停在旧位置，且没有任何重试被安排；
- `ma_sound_get_cursor_in_pcm_frames()` 在 `seekTarget` 尚未被混音线程消费时**直接返回目标值**（`miniaudio.h:79783-79786`）。因此界面上"显示新位置"与"连接器真的在新位置"是两回事。

---

## 2. 可复算的页号与帧位置

取 48 kHz、单声道/任意声道均可（帧数与声道无关）：

- 页大小 `P = 1000 ms × 48000/1000 = 48000` 帧；
- 录音总长 384000 帧 = 整 8 页（页 0～页 7），每页恰好 1 秒；
- 初始时刻作业线程已挂好页 0～3（覆盖帧 `[0, 192000)`），页 4 的解码作业正在运行，节点 `result = MA_BUSY`；
- 播放连接器此前播到帧 70000（页 1，页内偏移 22000）。

| 页号 | 绝对帧范围 | 页内偏移范围 | 初始状态 |
|---|---|---|---|
| 0 | `[0, 48000)` | 0..47999 | 已挂链 |
| 1 | `[48000, 96000)` | 0..47999 | 已挂链 |
| 2 | `[96000, 144000)` | 0..47999 | 已挂链 |
| 3 | `[144000, 192000)` | 0..47999 | 已挂链 |
| 4 | `[192000, 240000)` | 0..47999 | **解码中（旧预取）** |
| 5 | `[240000, 288000)` | 0..47999 | 未开始 |
| 6 | `[288000, 336000)` | 0..47999 | 未开始 |
| 7 | `[336000, 384000)` | 0..47999 | 未开始（末页） |

两次拖动：

- **拖动 ①**：目标 `T1 = 120500`，落在页 2（`96000 ≤ 120500 < 144000`），页内偏移 `120500 − 96000 = 24500`，该页已解码；
- **拖动 ②**（旧预取未结束时）：目标 `T2 = 220000`，落在页 4（`192000 ≤ 220000 < 240000`），页内偏移 `220000 − 192000 = 28000`，该页尚未挂链。

---

## 3. 从"显示新位置"到"交付新位置首帧"的逐次读取

混音线程每次处理回调向数据源发起读取（引擎处理缓存容量 `MA_DATA_CONVERTER_STACK_BUFFER_SIZE = 4096` 字节，`miniaudio.h:17565`；下文以每次 1024 帧为例，数字均可手工复算）。

### 3.1 拖动 ①（目标已在链表中）

`seek(120500)` 遍历哑头之后的页 0、页 1、页 2 即命中（`miniaudio.h:61055-61066`）：

- 返回 `MA_SUCCESS(0)`；
- `pCurrent = 页2`，`relativeCursor = 24500`，`absoluteCursor = 120500`；
- 数据缓冲层清掉 `seekTargetInPCMFrames`、`seekToCursorOnNextRead = FALSE`（`miniaudio.h:71766-71767`）。

若经 `ma_sound` 发起：`seekTarget` 原子量变成 120500，`ma_sound_get_cursor_in_pcm_frames()` 立刻回报 120500（界面已显示新位置），但连接器要等下一次混音回调在 `miniaudio.h:77174` 处执行 seek 后才真正移动。

### 3.2 拖动 ②（目标页尚未解码）——两条路径要分开看

**路径 A：直接调用 `ma_resource_manager_data_buffer_seek_to_pcm_frame()`（连接器已存在）**

- 进入 `ma_paged_audio_buffer_seek_to_pcm_frame(220000)`：前进遍历在页 3 结束后 `pNext == NULL`，循环退出，返回 `MA_BAD_SEEK(-25)`；
- 包装层在 `miniaudio.h:71762-71764` 直接把错误码返回，**不会**执行 71766-71767 的清场逻辑，`seekToCursorOnNextRead` 维持 `FALSE`；
- 连接器状态纹丝不动：`absoluteCursor` 仍是 120500，仍指向页 2。

**路径 B：经 `ma_sound_seek_to_pcm_frame()`**

- API 只做原子写并返回 `MA_SUCCESS`（`miniaudio.h:79711`），界面通过 get_cursor 立即显示 220000；
- 下一次混音回调执行 `ma_data_source_seek_to_pcm_frame(..., 220000)`，拿到 `MA_BAD_SEEK` 后**返回码被丢弃**，`seekTarget` 被无条件清空（`miniaudio.h:77174-77179`）；
- 结果：没有任何重试被安排，连接器继续停在 120500，后续读出的仍是拖动 ① 位置的声音。这是本版本里"显示跳了、声音没跳"的实际成因。

**延迟 seek 何时才会生效？** 只有两种情况会置位 `seekToCursorOnNextRead`：

1. seek 时连接器尚未创建（异步加载早期），在 `miniaudio.h:71755-71758` 记录目标并返回 `MA_BUSY`；
2. 已置位的延迟 seek 在 read 入口对分页连接器失败，于 `miniaudio.h:71671-71673` 被重新武装。

下面的逐读表按"延迟 seek 已武装、目标 220000"演示（路径 A/B 的失败结论不变，只是路径 B 没有自动重试）。

### 3.3 逐次读取表（页 4 解码完成前后）

| 序号 | 时机 | 调用与参数 | 连接器内发生了什么 | 拿到的 PCM 段 | framesRead | 返回码 | 含义 |
|---|---|---|---|---|---|---|---|
| R1 | 页 4 作业运行中 | read(1024)，入口先试延迟 seek | seek(220000) 遍历到链表尾失败 → `MA_BAD_SEEK`，重新武装延迟位（71671-71673） | 无 | **0** | **MA_BUSY(-19)** | 等数据；本次连读都没开始，游标保持 120500 |
| R2 | 同上 | read(1024) | 同 R1 | 无 | 0 | MA_BUSY | 等数据；`availableFrames = 192000 − 120500 = 71500` 也救不了，因为 seek 未落地 |
| R3 | 同上 | read(1024) | 同 R1 | 无 | 0 | MA_BUSY | 等数据 |
| — | 作业线程 | 页 4 解码完成 | `append_page` 挂入 `[192000,240000)`，`decodedFrameCount = 240000`，续投页 5 作业，节点仍 `MA_BUSY` | — | — | 作业内 SUCCESS→续期 | 链表新尾是页 4 |
| R4 | 页 4 挂链后首次 read | read(1024) | 延迟 seek 成功：`pCurrent=页4`、`relativeCursor=28000`、游标先变 220000；随后拷贝 1024 帧，游标变 221024，延迟位清除 | **`[220000, 221024)`** | **1024** | **MA_SUCCESS(0)** | **新位置首帧交付点**：此批声音才属于最后一次跳转 |
| R5 | 页 5 尚未挂链 | read(18976)（正好要到页 4 末端 240000） | 从 221024 拷到页 4 末尾，`pNext == NULL` → 连接器给 `MA_AT_END`；包装层见节点 `MA_BUSY` 改写为 `MA_BUSY`（71726-71730） | `[221024, 240000)` | **18976**（部分交付） | **MA_BUSY** | 本页余粮有效、下一页在等解码；声音层不会丢这批帧，它们留在 processing cache 下个回调处理（77270-77281） |
| R6 | 页 5 仍未挂链 | read(1024) | 游标已在页 4 末端，无页可读 → `MA_AT_END` → 改写 `MA_BUSY` | 无 | **0** | **MA_BUSY** | 等数据，与 R1-R3 同码，但此时游标是 240000 而非 120500 |
| — | 作业线程 | 页 5、6、7 依次完成 | 末页解码返回 `MA_AT_END`，作业归一化为 `MA_SUCCESS` 并 CAS 节点 result（73291-73296） | — | — | 节点转 `MA_SUCCESS` | 全片 384000 帧就位 |
| R7 | 全片解码完 | read(1024) | 页 5 已挂链，正常跨页拷贝 | `[240000, 241024)` | 1024 | MA_SUCCESS | 同样的调用位置，状态码随解码进度变化 |

### 3.4 "等数据"与"已经播完"的对照实验

同样在游标 383000 处请求 1024 帧（身后只剩 1000 帧）：

| 节点状态 | 调用 | framesRead | 返回码 | 判别 |
|---|---|---|---|---|
| `MA_BUSY`（页还没解码到这里） | read(1024) | 0 或部分 | **MA_BUSY(-19)** | 等数据：连接器层面其实是 `MA_AT_END`，被 71726-71730 改写 |
| `MA_SUCCESS`（全片解码完） | read(1024) | **1000**（PCM `[383000,384000)`） | **MA_AT_END(-17)** | 播完但本次有余粮：末批有效声音 |
| `MA_SUCCESS`，游标已在 384000 | read(1024) | **0** | **MA_AT_END(-17)** | 真正播完，无帧可给；声音层据此 `ma_sound_set_at_end` 并停声（77274-77276） |

辅助量：`ma_resource_manager_data_buffer_get_available_frames()` 对分页供给返回 `decodedFrameCount − absoluteCursor`（`miniaudio.h:71922-71934`）。它大于 0 只说明"已解码且在游标之后还有帧"，**不能证明游标已经在最后一次跳转的目标上**（R1 时刻该值为 71500，但声音仍是旧位置的）。

### 3.5 向后拖动的行为（对照）

若在预取向前跑的时候拖回 `T0 = 60000`（页 1，已解码）：seek 先复位到哑头再从页 0 重新走表（`miniaudio.h:61041-61048`），命中页 1、`relativeCursor = 12000`，返回 `MA_SUCCESS`。向后跳转不依赖作业线程，永远立即落地；链表只增不减也保证旧页不会被回收。

---

## 4. 帧数与返回状态的语义汇总

| 返回码 | framesRead | 在分页播放里的唯一含义 | 调用方动作 |
|---|---|---|---|
| `MA_SUCCESS(0)` | = 请求数 | 完整交付，且本次没有未落地的延迟 seek | 可直接使用 |
| `MA_BUSY(-19)` | **0** | 两种子情况：①延迟 seek 因目标页未挂链而未落地（71671）；②游标顶在已解码链表尾、下页解码中（71728）。两次都没拿到任何 PCM | 重试；不要把缓冲里旧数据当新位置声音 |
| `MA_BUSY(-19)` | **>0（部分）** | 本页余粮已交付，跨页时发现下页未挂链；这批帧本身是有效 PCM | 保留已读帧，剩余缺口下个周期补 |
| `MA_AT_END(-17)` | >0（部分） | 节点已 `MA_SUCCESS`，本批是录音最后的余粮 | 播放这批，随后收尾 |
| `MA_AT_END(-17)` | 0 | 节点已 `MA_SUCCESS` 且游标在录音末端，真正播完 | 停止/触发结束回调 |
| `MA_BAD_SEEK(-25)` | —— | seek 全程未执行，连接器三个状态字段保持原值 | 必须显式重试或重新安排；经 `ma_sound` 时该码被混音线程吞掉，需自行检测 |

补充两个失败边界：

- **seek 目标超过录音实际长度**：分页 seek 会永远找不到包含它的页。延迟 seek 路径在 71671 处不检查节点状态、无条件重新武装，于是即使节点已转 `MA_SUCCESS`，read 仍无限返回 `MA_BUSY/0`，不会变成 `MA_AT_END`；
- **连接器尚未创建时 seek**：API 只记录 `seekTargetInPCMFrames` 并返回 `MA_BUSY`（71755-71758），首帧交付同样要等连接器初始化后的第一次 read 把 seek 落地。

---

## 5. 调用方什么条件下可以相信"读到的声音属于最后一次跳转"

设最后一次跳转目标为 `T`，自 seek 落地以来累计已交付帧数为 `D`。

### 5.1 必要且充分的判定（数据缓冲/连接器层）

一次 read 返回的 PCM 可信，当且仅当同时满足：

1. **seek 已落地**：直接 seek 返回过 `MA_SUCCESS`；或延迟 seek 在某次 read 中执行后 `seekToCursorOnNextRead == MA_FALSE`（没有被 71672 重新武装）。仅仅 `ma_sound_get_cursor_in_pcm_frames() == T` 不算数——等待期间该 API 本来就回报 `T`（`miniaudio.h:79783-79786`）；
2. **本次 `framesRead > 0`**：`MA_BUSY/0` 与 `MA_AT_END/0` 都不交付任何样本；
3. **read 后读取 `absoluteCursor`，满足 `absoluteCursor == T + D`**：等价于本批首帧的绝对位置恰为 `T`（首批）或紧接上一批末尾。`MA_BUSY` 部分交付时游标前进量正好等于 `framesRead`，该等式逐批成立。

落地后首批可信数据的最简判据：

```
read(...) 得到 framesRead > 0，且 read 后 get_cursor() == T + framesRead
```

R4 是正例（220000 + 1024 = 221024）；R1-R3、R6 是反例（framesRead = 0，游标不动）。

### 5.2 各场景结论

| 场景 | 能否相信读到最后跳转的声音 | 原因 |
|---|---|---|
| 两次拖动都落在已解码页 | 能，第二次 seek 成功后首批 framesRead>0 即可信 | 连接器 seek 同步改游标，无歧义 |
| 最后一次跳到未解码页，走数据缓冲延迟 seek | 暂时不能；直到出现一次 `framesRead>0` 且游标从 `T` 开始前进 | 之前的 read 全部 `MA_BUSY/0`，不交付样本 |
| 最后一次跳到未解码页，走 `ma_sound` | **不能自动恢复**：`MA_BAD_SEEK` 在 77174 被丢弃，连接器留在旧位置，读出的声音属于上一次跳转 | 必须在应用层自己重试 seek（例如监听 `MA_BUSY`/轮询 `decodedFrameCount` 等价信息后重新调用 `ma_sound_seek_to_pcm_frame`） |
| 读到 `MA_BUSY` 但 framesRead>0 | 已读部分可信，缺口未补齐 | 游标等式对已读部分仍成立 |
| 节点 `MA_SUCCESS` 后读到 `MA_AT_END` | 可信，这是录音尾部真实数据 | 与"等数据"的唯一区别是节点 result 与返回码组合 |

### 5.3 工程上可操作的信任规则

1. 调用 `ma_sound_seek_to_pcm_frame()` 后记录目标 `T`，不要把 API 的 `MA_SUCCESS` 当作定位成功；
2. 在播放回调或监控点读取数据缓冲层结果：凡是 `MA_BUSY + framesRead==0` 一律视为"新位置尚未就绪"，不得消费；
3. 观察到 `framesRead > 0` 且 read 后游标 `== T + 累计交付帧` 时，才确认首帧来自最后一次跳转；仅看到游标等于 `T` 不够，必须看到它**从 `T` 向前推进**；
4. 用节点 result 区分两种零帧读取：`MA_BUSY` = 等解码（继续重试），`MA_AT_END` 且节点 `MA_SUCCESS` = 播完（结束流程）；
5. 若使用 `ma_sound` 且目标可能落在未解码区域，需要应用层自行承担重试（本版本混音线程吞掉 `MA_BAD_SEEK`），并警惕越过文件末端的目标会造成永久 `MA_BUSY`。

---

## 6. 复算指引

`research/sim/sim_paged_seek.py` 用纯 Python 等价实现了第 1.3 节的三个函数与分页作业，不含任何 miniaudio 依赖，按安全隔离方式运行：

```bash
python3 -I research/sim/sim_paged_seek.py
```

脚本输出与本文第 3 节的数字一一对应：48000 帧/页、拖动 ① 落页 2 偏移 24500、拖动 ② 返回 -25、三次 `MA_BUSY(-19)/0`、页 4 挂链后首批 1024 帧 `[220000,221024)`、页尾部分交付 18976 帧、EOF 处 `MA_AT_END(-17)/1000` 与 `/0`，以及向后跳转回 60000 的复位-遍历过程。修改脚本顶部的 `PAGE`、`TOTAL_FRAMES`、`T1`、`T2` 即可换一组采样率或录音长度重新复算。
