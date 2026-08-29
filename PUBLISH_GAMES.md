# AI Passport · 欢乐游戏厅 — 发布确认稿（game 固件）

> 来源工程：`ai-passport-games/`（CMake 工程名 `passport_games`，固件产物 `bambumonitor.bin`）
> 发布助手：`folotoy-ai-passport-publisher`（已安装并审计 P2 安全）

## 0. 执行状态
| 项 | 结果 |
| --- | --- |
| games 固件校验 | ✅ 通过（2,041,920 B，magic 0xE9，6 segments，sha256 `b323bb5b…`） |
| 封面校验 | ✅ 通过（1152×1536，精确 3:4，2,252,020 B） |
| 双语文案 | ✅ 已写（见第 1 节） |
| GitHub 建仓 | ⛔ 403 `Resource not accessible by integration` —— GitHub MCP 连接缺仓库写入权限（见第 3 节） |
| 社区 submit | ⛔ 阻塞：固件未实现 `FAP_SCREENSHOT_V1` 串口截图协议（见第 4 节） |

---

## 1. 公开文案（play-first，无技术规格）

**中文标题**：AI 通行证 · 欢乐游戏厅
**英文标题**：AI Passport · Pocket Arcade

**中文简介**：
把口袋里的 AI 通行证变成一台随身游戏机。多人模式举起它就能开派对——真心话大冒险、国王游戏、俄罗斯转盘、命运签，2 到 8 人轮流抽签，最后由「酒神」给今晚干杯收场。一个人也不无聊：打砖块、贪吃蛇、翻牌配对三款经典随时开玩，翻牌配对还有 3 档难度和常驻的 TOP3 排行榜。像素界面、按键操作、开机即有音乐，聚会、通勤、睡前都能来一局。

**英文简介**：
Turn your pocket AI Passport into a handheld game console. In Party mode it becomes an instant ice-breaker for 2–8 players — Truth or Dare, King's Game, Russian Roulette and Fate Draw, taking turns by random draw, then crowned by the "God of Wine" to close the night. Solo is never boring either: Breakout, Snake and Memory Match are always ready, with Memory offering three difficulty tiers and a persistent TOP-3 leaderboard. Pixel UI, button controls, music on boot — perfect for parties, commutes, or a round before sleep.

### 玩法要点（用于社区展示 / 封面构图）
- **多人派对**：真心话大冒险 · 国王游戏 · 俄罗斯转盘 · 命运签，先选 2–8 人，轮流抽签，结尾「酒神颁奖」结算页。
- **单人游戏**：打砖块（经典弹球破砖）、贪吃蛇（经典蛇）、翻牌配对（3 档难度 4/6/8 对，TOP3 榜存于设备）。
- **设置**：亮度等运行时设置页。
- **有趣之处**：派对模式把一块小屏变成聚会破冰工具；单人三游戏覆盖反应、策略、记忆；翻牌配对带本地排行榜，有重玩动力。

---

## 2. 封面
- 文件：`ai-passport-games/cover_1152x1536.png`（精确 3:4，1152×1536）
- 构图：像素游戏厅——打砖块 / 贪吃蛇 / 翻牌配对 / 派对元素，设备居中、霓虹辉光，不出现硬件参数。

---

## 3. GitHub 仓库（独立上传，via GitHub MCP）
- 所有者：`Bagel-EW`（https://github.com/Bagel-EW）
- 仓库名：`ai-passport-games`（公开，作为 game 固件的独立源码主页）
- 内容：源码 `main/`、`components/`、`docs/`、`tools/`、`tests/`、构建配置、`README.md`、`LICENSE`
- **不纳入**：`build*/`、`managed_components/`、`.vscode/`、`main/secrets.h`（含 Wi-Fi 凭据，已 gitignore）、固件二进制
- **固件二进制去向**：GitHub 内容 API 单文件 1MB 上限，`bambumonitor.bin`(≈2MB) 与 `font_zh14.c`(≈2.7MB) 无法经 MCP 直传；固件统一由 AI Passport 社区发布（publisher）分发，仓库 README 注明「固件请在社区刷入 / 从源码构建 / 运行 tools 字体生成脚本」。
- 仓库 URL（创建后回填）：`https://github.com/Bagel-EW/ai-passport-games`

### ⛔ 当前阻塞：GitHub MCP 无权建仓（HTTP 403）
- 报错：`POST https://api.github.com/user/repos: 403 Resource not accessible by integration`。
- 原因：当前连接的 GitHub 账号 `Bagel-EW` 对 WorkBuddy 集成**只授予了读权限**，没有仓库写入（`repo` / `public_repo` / Contents: write）作用域。`create_repository` 与后续的 `push_files` 都依赖该作用域，因此两条都会 403。
- **解决路径（任选其一，需你操作）**：
  1. **授予 MCP 写权限（推荐）**：在 GitHub → Settings → Developer settings / Applications 中找到 WorkBuddy 对应的授权，把 Repository contents 改为 **Read and write**（或重新授权时勾选 repo 权限）；或在 WorkBuddy 的 GitHub 连接器设置里重新连接并同意仓库写入范围。授权后告诉我，我立即建仓并推送。
  2. **提供 GitHub PAT**：给我一个具有 `repo` 作用域的 Personal Access Token，我用本地 `git` 直接推（Token 仅用于本次推送，不会写入任何文件）。
  3. **你手动建仓**：你在 GitHub 网页建好公开仓库 `ai-passport-games`，但注意 MCP 仍因缺写入权限无法推送；所以优先走 1 或 2。

- 仓库内容已规划好（源码 + README + LICENSE + docs + tools + tests），权限到位后我会一次性 `create_repository` + 分批 `push_files` 完成「独立上传」。

---

## 4. AI Passport 社区发布状态（publisher）
- `validate` 固件：✅ 通过（`bambumonitor.bin`，magic 0xE9，6 segments）
- `validate` 封面：✅ 通过（1152×1536，精确 3:4）
- **⚠️ 阻塞项：`FAP_SCREENSHOT_V1` 串口截图协议未实现**
  - `publisher.py` 的 `validate` 与 `submit` 均强制要求 `--screen-capture`（由 `capture-screen` 从真机抓取并生成收据）。
  - 当前 games 固件未实现该协议（grep 全树无 `FAP_SCREENSHOT_V1` 真正实现），因此：
    - 无法生成有效截图收据 → `validate`/`submit` 会报 "Serial capture receipt is missing"。
  - **解决路径（需你确认是否执行）**：在固件中加入 `FAP_SCREENSHOT_V1` 响应（收到命令后回传 RGB565 帧缓冲），重新编译，连接设备后用 `publisher.py capture-screen` 抓取，再 `submit --confirmed`。
- 授权：若未登录，需先在 https://ai-passport.folotoy.cn/account/ 注册/登录并授权（此前设备代码已生成，过期可重跑 `publisher.py authorize`）。

---

## 5. 待你确认 / 需你操作
1. **中文/英文标题与简介是否满意**（是否调整措辞）。
2. **GitHub 上传阻塞**：当前 MCP 无仓库写入权限（403）。请按第 3 节「解决路径」授予写权限或提供 PAT —— 之后我建仓并推送。仓库名 `ai-passport-games`、公开可见性是否 OK？
3. **社区 submit 阻塞**：games 固件未实现 `FAP_SCREENSHOT_V1` 协议。是否由我：
   - 在固件中加入串口截图响应（收到 `FAP_SCREENSHOT_V1` 后回传 RGB565 帧缓冲）；
   - 重新编译 games 固件（约 2 小时全量，或增量）；
   - 你连接设备后我用 `publisher.py capture-screen` 抓屏生成收据，再 `submit --confirmed`。
   或你自行处理该协议。
4. 两条阻塞解除后，我再执行正式上传（社区 `submit --confirmed` 需截图收据就绪）。
