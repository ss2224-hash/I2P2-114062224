# MinitChess (5x6) AI Bot 系統架構與實作指引

本文件概述了基於 Minimax 演算法的 MinitChess (5x6) AI 機器人設計架構。結合了直觀的陣列結構與高效的位元運算，並針對特定的時間與硬體限制提出實作策略。

---

## 1. 系統限制與對應策略

* **時間限制：每步 10 秒**
    * **策略：** 10 秒對於 5x6 棋盤的位元運算 AI 來說是非常充裕的時間。為了充分利用這 10 秒且避免超時，應實作**迭代加深 (Iterative Deepening)**。AI 會先搜尋深度 1，再搜尋深度 2，依序遞增，直到時間耗盡前（例如設定在 9.5 秒時強制中斷），回傳當下找到的最佳解。
* **記憶體限制：4GB**
    * **策略：** 4GB 的記憶體非常龐大。強烈建議實作**置換表 (Transposition Table, TT)**，利用雜湊 (Zobrist Hashing) 將計算過的盤面分數存入記憶體中。你可以輕鬆開出 1GB~2GB 的 Hash Table，這將極大幅度地減少重複計算，讓搜尋深度產生質的飛躍。

---

## 2. 核心資料結構 (Hybrid Representation)

系統採用「混合式 (Hybrid)」資料結構，以兼顧狀態評估的直觀性與合法步生成的極致效能。

### 2.1 雙層 2D 陣列 (狀態與評估用)
```cpp
// player: 0 (白方), 1 (黑方)
// row: 0~5 (共 6 列), col: 0~4 (共 5 欄)
char board[2][BOARD_H][BOARD_W];
```
* **優勢：** 時間複雜度 $O(1)$ 的空間查詢。
* **用途：** 用於 UI 渲染、基礎邏輯判斷，以及在「靜態評估函數」中快速查表 (PST) 計算位置分數。

### 2.2 30-bit 位元棋盤 (合法步生成用)
* **結構：** 使用 `uint32_t`，利用前 30 個 bits 對應 6x5 棋盤的 30 個格子 (Index = `row * 5 + col`)。
* **優勢：** 利用位元運算 (`&`, `|`, `~`, `<<`, `>>`) 進行平行運算。
* **用途：** 透過預先計算好的攻擊遮罩 (如 `bb_knight`, `bb_king`, `bb_pawn_push`)，以 $O(1)$ 的時間複雜度配合位元遮罩快速生成所有合法步。

---

## 3. 核心搜尋引擎

### 3.1 Minimax 演算法與 Alpha-Beta 剪枝
AI 決策的核心基於賽局樹的深度優先走訪。

* **Max 節點 (AI 本身)：** 選擇讓評估分數最大化的分支。
* **Min 節點 (對手)：** 選擇讓評估分數最小化的分支。
* **Alpha-Beta 剪枝：**
    * `alpha`：Max 節點目前保證能拿到的最低分數。
    * `beta`：Min 節點目前保證能拿到的最高分數。
    * 若在走訪中發現 `score >= beta` (對 Max 而言) 或 `score <= alpha` (對 Min 而言)，則直接觸發剪枝 (Pruning)，停止走訪該分支，大幅節省時間。

### 3.2 虛擬碼架構
```cpp
int minimax(Board board, int depth, int alpha, int beta, bool isMax) {
    if (depth == 0 || isGameOver(board)) {
        return evaluate(board);
    }
    
    // 使用位元棋盤生成合法步
    std::vector<Move> moves = get_legal_actions_bitboard(board);
    
    if (isMax) {
        int maxEval = -INFINITY;
        for (Move m : moves) {
            Board nextBoard = makeMove(board, m);
            int eval = minimax(nextBoard, depth - 1, alpha, beta, false);
            maxEval = max(maxEval, eval);
            alpha = max(alpha, eval);
            if (beta <= alpha) break; // Beta 剪枝
        }
        return maxEval;
    } else {
        int minEval = INFINITY;
        for (Move m : moves) {
            Board nextBoard = makeMove(board, m);
            int eval = minimax(nextBoard, depth - 1, alpha, beta, true);
            minEval = min(minEval, eval);
            beta = min(beta, eval);
            if (beta <= alpha) break; // Alpha 剪枝
        }
        return minEval;
    }
}
```

---

## 4. 靜態評估函數 (Static Evaluation)

當搜尋到達深度限制時，負責替當下盤面打分的函數 `evaluate(board)`。綜合分數由以下三部分組成：

### 4.1 材質計分 (Material Score)
最基礎的子力價值計算。
* 兵 (Pawn) = 100
* 馬 (Knight) = 300
* 象 (Bishop) = 320
* 車 (Rook) = 500
* 后 (Queen) = 900
* 王 (King) = 10000

### 4.2 棋子-位置評分表 (Piece-Square Tables, PST)
為每種棋子建立 6x5 的二維陣列，給予控制中央或推進的額外加分。
* **實作細節：** 建立一套白方視角的基準表。當評估黑方 (`board[1]`) 時，將列數 (Row) 翻轉 (`5 - row`) 即可共用同一張表。
* **範例 (馬的 PST)：** 邊角為負分，中央區域為正分。
### 4.2 棋子-位置評分表 (Piece-Square Tables, PST)
**視角說明**：以下表格皆為**「白方視角」**（從 `row=5` 往 `row=0` 推進）。計算黑方時，請將 `row` 翻轉為 `5 - row`。

```cpp
// 兵 (Pawn)：越靠近敵方底線價值越高
const int PST_Pawn[6][5] = {
    { 50,  50,  50,  50,  50}, // Row 0 (即將或已經升變)
    { 30,  30,  40,  30,  30}, // Row 1 
    { 10,  15,  25,  15,  10}, // Row 2 (跨過半場，控制中央)
    {  5,   5,  10,   5,   5}, // Row 3 
    {  0,   0,   0,   0,   0}, // Row 4 (初始位置)
    {  0,   0,   0,   0,   0}  // Row 5 
};

// 馬 (Knight)：必須佔據中央，邊角極弱
const int PST_Knight[6][5] = {
    {-15, -10, -10, -10, -15}, 
    {-10,   0,   5,   0, -10},
    { -5,   5,  15,   5,  -5}, // 中央最高分
    { -5,   5,  15,   5,  -5},
    {-10,   0,   5,   0, -10},
    {-15, -10, -10, -10, -15}
};

// 象 (Bishop)：控制長斜線
const int PST_Bishop[6][5] = {
    {-10,  -5,  -5,  -5, -10},
    { -5,  10,   5,  10,  -5}, 
    { -5,   5,  15,   5,  -5}, 
    { -5,   5,  15,   5,  -5},
    { -5,  10,   5,  10,  -5}, 
    {-10,  -5,  -5,  -5, -10}
};

// 車 (Rook)：喜歡開放線與對手的次底線
const int PST_Rook[6][5] = {
    {  0,   0,   0,   0,   0},
    { 20,  20,  20,  20,  20}, // 佔據次底線橫掃敵兵
    { -5,   0,   5,   0,  -5},
    { -5,   0,   5,   0,  -5},
    { -5,   0,   5,   0,  -5},
    {  0,   0,   5,   0,   0}  
};

// 后 (Queen)：不宜太早衝入敵陣
const int PST_Queen[6][5] = {
    { -5,  -5,  -5,  -5,  -5},
    { -5,   0,   0,   0,  -5},
    { -5,   0,   5,   0,  -5},
    { -5,   0,   5,   0,  -5},
    { -5,   0,   0,   0,  -5},
    { -5,  -5,  -5,  -5,  -5}
};

// 王 (King) - 中局：需要躲避火力，待在邊角最安全
const int PST_King_Midgame[6][5] = {
    {-30, -30, -30, -30, -30}, 
    {-30, -30, -30, -30, -30},
    {-20, -20, -20, -20, -20},
    {-15, -15, -15, -15, -15},
    { -5,  -5, -10,  -5,  -5},
    { 15,  15, -10,  15,  15}  // 躲在底線兩側最安全
};
```


### 4.3 機動性評分 (Mobility)
計算棋子當下擁有的合法步數量。
* **實作細節：** 搭配位元棋盤的攻擊遮罩，利用 `__builtin_popcount(valid_moves_bitboard)` 快速算出有幾個 bit 為 1。
* **應用：** 每多一個可走格子給予微小加分 (例如 +2 分)，鼓勵 AI 將棋子 (特別是車與后) 移動到開闊的控制線上。
機動性評分能讓 AI 理解「控制空間」的價值，鼓勵它將棋子走到開闊且具備威脅性的位置，而非只是死板地依賴位置分數 (PST) 查表。

---

## 1. 核心概念與常見陷阱

* **定義：** 機動性 = 該棋子當下擁有的**合法可移動格數**。
* **計算公式：** `機動性加分 = 可移動格數 × 該兵種專屬乘數`
* **陷阱警告：** 機動性分數必須是**微小的附加價值**。如果乘數過高，AI 會誤以為「獲得更多移動空間」比「保護棋子」更重要，導致它為了追求高機動性而把后或車送到會被吃掉的地方。

---

## 2. 兵種專屬機動性乘數 (Mobility Multipliers)

針對 5x6 棋盤，搭配基準材質分數（兵=100, 車=500）的建議乘數設定如下：

| 兵種 | 乘數 | 實作邏輯與考量 |
| :--- | :--- | :--- |
| **馬 (Knight)** | **`+3 分` / 格** | 馬在邊角能力極弱，在 5x6 棋盤中最多跳 8 格。此乘數能有效鼓勵 AI 將馬跳向中央，最大加分為 +24 分。 |
| **象 (Bishop)** | **`+4 分` / 格** | 象的價值完全取決於「開放斜線」。這是最高的乘數，用以強烈懲罰被己方兵擋住的「壞象」，並鼓勵 AI 尋找長斜線架砲。 |
| **車 (Rook)** | **`+3 分` / 格** | 車在完全開放線上最多可走 9 格 (+27分)。這足以誘使 AI 積極搶佔開放直線與次底線。 |
| **后 (Queen)** | **`+1 分` / 格** | **極度危險！** 后的天然機動性極高。如果乘數過高，AI 在開局就會把后亂衝出去。給予微小的 +1 分作為微調即可。 |
| **兵 (Pawn)** | **`0 分`** | 兵的價值看「兵形」與推進深度 (由 PST 處理)，不計算機動性。 |
| **王 (King)** | **`0 分`** | 國王在中局亂跑是大忌，殘局的活躍度也由 PST 處理，不計算機動性。 |

---

## 3. 實作方法 (基於 Bitboard)

利用位元運算與 `__builtin_popcount` 可以用 O(1) 的極速算出機動性。

### 3.1 基礎機動性 (Basic Mobility)
只排除「會踩到自己人」的格子。

```cpp
// 假設 bb_knight 是預先算好的 30-bit 攻擊遮罩陣列
// index 是該馬在 5x6 棋盤上的 0~29 索引值
// friendly_pieces 是己方所有棋子的 bitboard

uint32_t valid_moves = bb_knight[index] & (~friendly_pieces);
int mobility = __builtin_popcount(valid_moves);
int score_bonus = mobility * 3; // 馬的乘數為 3
```

### 3.2 進階優化：安全機動性 (Safe Mobility) - 強烈建議！
基礎機動性有個缺點：AI 可能會把算出來的合法步，走到**已經被對手小兵控制的死路**上。
實作「安全機動性」，也就是在算 `valid_moves` 時，**剔除掉對手兵的攻擊範圍**。

```cpp
// 1. 預先算出對手所有「兵」的攻擊遮罩聯集 (Enemy Pawn Attack Mask)
uint32_t enemy_pawn_attacks = 0;
// 遍歷對手所有的兵，將它們的攻擊遮罩做 OR 運算聯集起來...

// 2. 計算安全合法步
// (馬的原始攻擊範圍) AND (不能是自己人) AND (不能是對手兵的攻擊範圍)
uint32_t valid_safe_moves = bb_knight[index] & (~friendly_pieces) & (~enemy_pawn_attacks);

// 3. 計算分數
int safe_mobility = __builtin_popcount(valid_safe_moves);
int score_bonus = safe_mobility * 3;
```
*效果：只要加上這一個 `& (~enemy_pawn_attacks)` 位元運算，你的 AI 走位就會瞬間變得像人類一樣聰明，絕不會把大子白白送給對手的小兵吃。*

---

## 4. 滑動子 (Sliding Pieces) 的機動性計算

象、車、后被稱為「滑動子」，它們的攻擊射線會被**任何棋子 (包含敵我)** 擋住。
因此，你不能像馬一樣直接查靜態的 `bb_knight` 表。

**在評估函數中的實作建議：**
你有兩種方式來取得滑動子當下的 `valid_moves`：

1.  **迴圈射線法 (較慢但直觀)：** 從棋子所在座標出發，沿著 2D 陣列 (`board[2][6][5]`) 的方向迴圈往外找，碰到任何棋子就停，計算走過的格數。
2.  **Magic Bitboards / 預算查表法 (極速)：** 這是高階引擎的做法。利用目前盤面上所有棋子的佔用狀態 (`all_pieces_bitboard`) 作為 Hash Key，去查表瞬間得到該滑動子目前的攻擊遮罩。

一旦取得滑動子實際的攻擊遮罩，再套用前面的 `__builtin_popcount` 算出格數並乘上對應的乘數即可。

### 4.4 兵形分析 (Pawn Structure)

藉由檢查每一欄 (Column) 的兵來給予微調分數：
* **疊兵 (Doubled Pawns) 懲罰： `-15 分`**
    * **判定：** 同一欄中有超過 1 個己方的兵。
* **孤兵 (Isolated Pawn) 懲罰： `-20 分`**
    * **判定：** 該兵所在的欄，其「左邊一欄」跟「右邊一欄」完全沒有己方的兵。
* **通路兵 (Passed Pawn) 獎勵： `+30 分`**
    * **判定：** 該兵的前方 (包含正前方同欄、左前、右前一欄) 已經沒有任何敵方兵可阻擋。

### 4.5 國王安全 (King Safety)

在 5x6 的小空間裡，攻王速度極快，國王前方的屏障極度重要：
* **兵盾破壞 (Missing Pawn Shield) 懲罰： 每個缺失的兵 `-20 分`**
    * **判定：** 檢查國王正前方的 3 個格子 (左前、正前、右前)，若無己方兵保護則扣分。
* **半開放/全開放線 (Open File against King) 懲罰：**
    * **全開放線 `-30 分`：** 國王所在欄沒有任何兵，敵車可長驅直入。
    * **半開放線 `-15 分`：** 國王所在欄沒有己方的兵，但有敵方的兵。
---

## 5. 開發與實作步驟建議

1.  **基礎建設：** 確認 `board[2][6][5]` 與 `uint32_t bitboard` 的狀態同步與轉換正確無誤。
2.  **合法步驗證：** 確保 `get_legal_actions_bitboard()` 產生的步數與暴力法產生的結果完全一致 (可寫 Test Case 驗證)。
3.  **基礎 Minimax：** 實作帶有 Alpha-Beta 剪枝的 Minimax，並只使用「材質計分」進行測試。
4.  **加入位置觀念：** 引入 PST 表格與機動性評估，觀察 AI 的下棋風格是否變得更有戰略性。
5.  **壓榨效能 (選配)：** 引入 Iterative Deepening 與 Zobrist Hashing，確保在 10 秒內算到最深極限。