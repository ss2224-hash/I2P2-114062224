# MinitChess AI 引擎：`MiniMax::search` 核心架構解析

`MiniMax::search` 函數是整個 AI 大腦的**「總司令部（Entry Point / Root Node）」**。

底層的 `eval_ctx` 與 `quiescence_search` 只負責算分數（回傳 `int`），而 `search` 函數的唯一使命，就是把第一層（根節點）的每一種可能走法都展開，讓底層去算分，然後**把分數最高的那「一步棋 (Move)」記錄下來並回傳給遊戲系統**。

以下將此函數拆解為 5 個主要階段進行詳細解析：

---

## 🛠️ 第一階段：重置狀態與防呆檢查

在每次輪到 AI 思考時，必須先將搜尋環境初始化，並處理極端情況。

```cpp
ctx.reset();
MMParams p = MMParams::from_map(ctx.params);
SearchResult result;
result.depth = depth;

if(!state->legal_actions.size()){
    state->get_legal_actions();
}

// 防呆：如果已經贏了或平手，直接打包回傳，不浪費時間算
if(state->game_state == WIN){
    result.score = P_MAX - state->step;
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    result.pv.clear();
    return result;
}
if(state->game_state == DRAW){ ... }
```

* **`ctx.reset()`**：每次收到使用者的下棋指令，開始全新搜尋時，把節點計數器 (`nodes`) 等全域資訊歸零。
* **`SearchResult result`**：這是我們要回傳的最終包裹，裡面會裝 `best_move`（最佳棋步）、`score`（預測分數）、`nodes`（思考了幾個節點）等資訊。

---

## 📊 第二階段：初始化邊界與步法排序 (Root Move Ordering)

就算是在第一層（根節點），**步法排序 (Move Ordering)** 一樣超級重要！

```cpp
int best_score = M_MAX; 
int alpha = M_MAX;      // 初始極小值 (通常代表 -infinity)
int beta = P_MAX;       // 初始極大值 (通常代表 +infinity)
int move_index = 0;
int total_moves = (int)state->legal_actions.size();

// 將第一層的所有合法步依照 MVV-LVA (吃子價值) 進行排序
std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&state](const auto& a, const auto& b) {
    return score_action(state, a) > score_action(state, b);
});
```

* 如果你在根節點第一步就找到了超級神仙步（例如吃掉對手皇后），它會產生一個極高的 `alpha`。
* 當 AI 接著去看根節點的第二步、第三步時，底層的 **Alpha-Beta 剪枝** 就會像割草機一樣瘋狂砍掉沒用的樹枝，幫你省下巨量時間。

---

## 🚀 第三階段：展開第一層與極速狀態池

```cpp
bool first_move = true;

for(auto& action : state->legal_actions){

    // 使用零記憶體分配的「狀態池」
    State* next = &state_stack[0];
    state->apply_move(action, *next); 

    int score;
    // ... 準備交給底層評分
```

* 這個 `for` 迴圈是整個函數的核心，它負責把第一步能走的所有棋（例如 20 種走法）逐一拿出來實驗。
* **`&state_stack[0]`**：因為 `search` 函數永遠身處在第 0 層，所以我們永遠拿 `state_stack[0]` 這個空盒子，用 `apply_move` 把盤面寫進去，準備交給下一層。這徹底免除了 `new` / `delete` 的效能開銷。

---

## 🧠 第四階段：根節點的主變例搜尋 (PVS)



把最困難的遞迴交給底層的 `eval_ctx`，並利用 **PVS (Principal Variation Search)** 極大化搜尋效率。

```cpp
    if (first_move) {
        // 第一步：全視窗搜尋 (Full Window Search)
        int raw = eval_ctx(next, depth - 1, history, 1, ctx, p, -beta, -alpha);
        score = -raw;
        first_move = false;
    } else {
        // 後續步：零視窗試探 (Null Window Search)
        int raw = eval_ctx(next, depth - 1, history, 1, ctx, p, -alpha - 1, -alpha);
        score = -raw;

        // 若意外發現比第一步好，重新用全視窗搜尋
        if (score > alpha && score < beta) {
            raw = eval_ctx(next, depth - 1, history, 1, ctx, p, -beta, -score);
            score = -raw;
        }
    }
```

* **為什麼不讓 `search` 呼叫自己？** 因為 `search` 的負擔太重了（要記錄路徑、回報 UI），如果讓它一直遞迴，效能會很差。標準做法是把髒活累活全部丟給極度輕量化的 `eval_ctx` 去做。
* **PVS 邏輯：** 假設第一步最好，給予完整視窗 `[-beta, -alpha]`。後面的步給予極窄的零視窗 `[-alpha-1, -alpha]` 來快速驗證它們是爛棋（觸發剪枝）。

---

## 🛡️ 第五階段：絕對保底機制與 GUI 回報

```cpp
    // 絕對保底機制：就算分數再爛，也要無條件接受第一步
    if(move_index == 0 || score > best_score){
        best_score = score;
        result.best_move = action;
        result.score = best_score;
        result.pv = {action};

        if (best_score > alpha) {
            alpha = best_score; // 提高保底門檻
        }

        // 向使用者/畫面回報搜尋進度
        if(p.report_partial && ctx.on_root_update){
           ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
        }
    }
    move_index++;
} // end of for loop

// 打包回傳最終結果
result.nodes = ctx.nodes;
result.seldepth = ctx.seldepth;
result.depth = depth;
return result;
```

* **`move_index == 0` 的重要性：** 想像一下，如果 AI 面臨必輸的局面，所有步的分數都是負無限大。如果沒有這個條件，AI 會找不到「更好」的分數而回傳一個「空」的棋步導致遊戲崩潰。這個條件確保了 **AI 至少會無條件接受第一步作為最爛的備案**。
* **`ctx.on_root_update`：** 這是引擎與外界溝通的橋樑。它會在螢幕上印出類似：「目前看深度 5，覺得走馬最好，分數是 150，算到第 3/20 種可能...」讓使用者知道 AI 正在思考且沒有當機。

---

## 🎯 總結：總司令部的工作哲學

`search` 函數就像是公司裡的**「部門經理」**：
1. 接收總老闆（使用者）的命令。
2. 將所有可行的方案（Legal Actions）列出來並排序。
3. 把方案一個個派發給底下的「苦力工程師（`eval_ctx`）」去執行推演。
4. 當所有工程師回報分數後，經理挑選出分數最高的那個方案（`best_move`），打包成精美的報告（`SearchResult`）呈交給總老闆。