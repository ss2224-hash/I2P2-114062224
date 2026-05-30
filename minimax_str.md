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

1. 核心概念與常見陷阱

* **定義：** 機動性 = 該棋子當下擁有的**合法可移動格數**。
* **計算公式：** `機動性加分 = 可移動格數 × 該兵種專屬乘數`
* **陷阱警告：** 機動性分數必須是**微小的附加價值**。如果乘數過高，AI 會誤以為「獲得更多移動空間」比「保護棋子」更重要，導致它為了追求高機動性而把后或車送到會被吃掉的地方。

---

2. 兵種專屬機動性乘數 (Mobility Multipliers)

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

3. 實作方法 (基於 Bitboard)

利用位元運算與 `__builtin_popcount` 可以用 O(1) 的極速算出機動性。

3.1 基礎機動性 (Basic Mobility)
只排除「會踩到自己人」的格子。

```cpp
// 假設 bb_knight 是預先算好的 30-bit 攻擊遮罩陣列
// index 是該馬在 5x6 棋盤上的 0~29 索引值
// friendly_pieces 是己方所有棋子的 bitboard

uint32_t valid_moves = bb_knight[index] & (~friendly_pieces);
int mobility = __builtin_popcount(valid_moves);
int score_bonus = mobility * 3; // 馬的乘數為 3
```

3.2 進階優化：安全機動性 (Safe Mobility) - 強烈建議！
基礎機動性有個缺點：AI 可能會把算出來的合法步，走到**已經被對手小兵控制的死路**上。
實作「安全機動性」，也就是在算 `valid_moves` 時，**剔除掉對手兵的攻擊範圍**。

    1. 高效位元棋盤 (Bitboard) 實作

    在 5x6 棋盤中，計算對手小兵的攻擊火力網不需要經過耗時的陣列雙層迴圈，可直接利用位元位移 (Bitwise Shift) 在 $O(1)$ 時間內完成。

    1.1 敵方小兵攻擊遮罩生成 (白方視角)
    當 AI 為白方時，需要計算黑兵的攻擊範圍（黑兵向前推進為 Row 增加，即 Index 變大）。

    ```cpp
    // 5x6 Bitboard 邊界遮罩 (防止從左邊界或右邊界溢出到下一列)
    const uint32_t FILE_A = 0x10842108; // 欄 0 的所有 bits
    const uint32_t FILE_E = 0x02108421; // 欄 4 的所有 bits

    uint32_t get_black_pawn_attacks(uint32_t black_pawns) {
        uint32_t attacks = 0;
        // 黑兵向左下方攻擊 (Index + 4)，需排除原本就在 A 欄的兵溢出
        attacks |= (black_pawns << 4) & (~FILE_E);
        // 黑兵向右下方攻擊 (Index + 6)，需排除原本就在 E 欄的兵溢出
        attacks |= (black_pawns << 6) & (~FILE_A);
        return attacks;
    }
    ```
    1. 補齊「安全機動性 (Safe Mobility)」的黑方視角邏輯
    在標準 Minimax 裡，你也需要評估黑方棋子，所以必須反過來算出「白兵攻擊範圍」。

    白兵是往 Row 減少的方向前進（對應到 1D Index 是減少），因此位元運算要改用右移 (>>)，且邊界遮罩的方向剛好相反：

    ```cpp
    // 5x6 Bitboard 邊界遮罩
    const uint32_t FILE_A = 0x10842108; // 欄 0 (Col 0)
    const uint32_t FILE_E = 0x02108421; // 欄 4 (Col 4)

    // 取得黑兵火力網 (保護白子用)
    uint32_t get_black_pawn_attacks(uint32_t black_pawns) {
        uint32_t attacks = 0;
        attacks |= (black_pawns << 4) & (~FILE_E); // 往左下攻擊
        attacks |= (black_pawns << 6) & (~FILE_A); // 往右下攻擊
        return attacks;
    }

    // 取得白兵火力網 (保護黑子用) - 這是你需要補上的！
    uint32_t get_white_pawn_attacks(uint32_t white_pawns) {
        uint32_t attacks = 0;
        // 白兵往左上攻擊 (Index - 6)，位元右移
        attacks |= (white_pawns >> 6) & (~FILE_E); 
        // 白兵往右上攻擊 (Index - 4)，位元右移
        attacks |= (white_pawns >> 4) & (~FILE_A); 
        return attacks;
    }
    ```
---
4 滑動子 (Sliding Pieces) 的機動性計算

象、車、后被稱為「滑動子」，它們的攻擊射線會被**任何棋子 (包含敵我)** 擋住。
因此，你不能像馬一樣直接查靜態的 `bb_knight` 表。

這份指南提供了在 5x6 棋盤上最穩定、極速的滑動子機動性計算方式 (Raycasting)。利用短迴圈配合 2D 陣列，避免了複雜的位元板碰撞計算。



    1. 定義射線方向與乘數

    * **車 (Rook)：** 上、下、左、右 (4 個方向)。乘數：**`+3 分/格`**
    * **象 (Bishop)：** 四個斜角 (4 個方向)。乘數：**`+4 分/格`**
    * **后 (Queen)：** 直線加斜線 (8 個方向)。乘數：**`+1 分/格`** (后極易過度活躍，乘數必須最低)

    ---

    2. 射線掃描輔助函數 (Raycasting Helper)

    請將這個輔助函數加在你的 `evaluateBoard_Standard` 函數之前。它的邏輯非常簡單：從棋子當前位置出發，沿著特定方向一格一格看，碰到自己人就停，碰到敵人算 1 步然後停，碰到空格算 1 步並繼續前進。

    ```cpp
    // 預先定義好方向向量
    const int DIR_ROOK_R[4] = {-1, 1, 0, 0};
    const int DIR_ROOK_C[4] = {0, 0, -1, 1};
    const int DIR_BISHOP_R[4] = {-1, -1, 1, 1};
    const int DIR_BISHOP_C[4] = {-1, 1, -1, 1};
    const int DIR_QUEEN_R[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    const int DIR_QUEEN_C[8] = {0, 0, -1, 1, -1, 1, -1, 1};

    // 計算單一滑動子的機動性格數
    // player: 0 (白), 1 (黑)
    int get_slider_mobility(Board& b, int r, int c, int player, const int dir_r[], const int dir_c[], int dir_count) {
        int mobility = 0;
        
        for (int i = 0; i < dir_count; i++) {
            int nr = r + dir_r[i];
            int nc = c + dir_c[i];
            
            while (nr >= 0 && nr < 6 && nc >= 0 && nc < 5) {
                // 1. 如果撞到自己的棋子，這條射線就被擋住了，換下個方向
                if (b.board[player][nr][nc] != '\0') {
                    break;
                }
                
                // 2. 這格是空格，或是敵人的棋子，機動性 +1
                mobility++;
                
                // 3. 如果撞到敵人的棋子，雖然可以吃，但射線無法穿透，換下個方向
                if (b.board[1 - player][nr][nc] != '\0') {
                    break;
                }
                
                // 4. 繼續沿著同方向往下一格看
                nr += dir_r[i];
                nc += dir_c[i];
            }
        }
        
        return mobility;
    }
    ```

    ---

    3. 整合進靜態評估函數 (State Value Function)

    現在，你可以直接在原本計算材質與 PST 的雙層迴圈中，呼叫這個輔助函數，並乘上對應的分數。

    ```cpp
    int evaluateBoard_Standard(Board& b) {
        int white_score = 0;
        int black_score = 0;
        
        // (省略材質計算與快速短路，參考前一份文件...)

        for (int r = 0; r < 6; r++) {
            for (int c = 0; c < 5; c++) {
                // --- 白方掃描 ---
                char wp = b.board[0][r][c];
                if (wp != '\0') {
                    // (省略 PST 查表...)
                    
                    // 計算滑動子機動性 (白方 player = 0)
                    if (wp == 'R') {
                        int mob = get_slider_mobility(b, r, c, 0, DIR_ROOK_R, DIR_ROOK_C, 4);
                        white_score += (mob * 3);
                    }
                    else if (wp == 'B') {
                        int mob = get_slider_mobility(b, r, c, 0, DIR_BISHOP_R, DIR_BISHOP_C, 4);
                        white_score += (mob * 4);
                    }
                    else if (wp == 'Q') {
                        int mob = get_slider_mobility(b, r, c, 0, DIR_QUEEN_R, DIR_QUEEN_C, 8);
                        white_score += (mob * 1);
                    }
                }
                
                // --- 黑方掃描 ---
                char bp = b.board[1][r][c];
                if (bp != '\0') {
                    // (省略 PST 查表...)
                    
                    // 計算滑動子機動性 (黑方 player = 1)
                    // 注意：機動性分數對黑方來說是「增加他自己的優勢」，所以對白方視角來說要用扣的
                    if (bp == 'r') {
                        int mob = get_slider_mobility(b, r, c, 1, DIR_ROOK_R, DIR_ROOK_C, 4);
                        black_score += (mob * 3); 
                    }
                    else if (bp == 'b') {
                        int mob = get_slider_mobility(b, r, c, 1, DIR_BISHOP_R, DIR_BISHOP_C, 4);
                        black_score += (mob * 4);
                    }
                    else if (bp == 'q') {
                        int mob = get_slider_mobility(b, r, c, 1, DIR_QUEEN_R, DIR_QUEEN_C, 8);
                        black_score += (mob * 1);
                    }
                }
            }
        }

        return white_score - black_score;
    }
    ```
---



## 5. 開發與實作步驟建議

1.  **基礎建設：** 確認 `board[2][6][5]` 與 `uint32_t bitboard` 的狀態同步與轉換正確無誤。
2.  **合法步驗證：** 確保 `get_legal_actions_bitboard()` 產生的步數與暴力法產生的結果完全一致 (可寫 Test Case 驗證)。

## 完成狀態

此文件已撰寫並標註為「已完成」。
3.  **基礎 Minimax：** 實作帶有 Alpha-Beta 剪枝的 Minimax，並只使用「材質計分」進行測試。
4.  **加入位置觀念：** 引入 PST 表格與機動性評估，觀察 AI 的下棋風格是否變得更有戰略性。

    如何從主程式呼叫 Minimax (Root Call)
    你的 standard_minimax 回傳的是一個整數（分數），但你在遊戲中真正需要 AI 吐出來的是一個具體的棋步 (Move)。因此，你需要一個外層的 Wrapper 函數來啟動 Minimax，並結合時間限制，這才是真正要交作業的主程式入口：

    ```cpp
    Move get_best_move_standard(Board& board, bool isWhite, int time_limit_ms = 9500) {
    long long start_time = get_time_ms(); // 取得當下時間
    Move best_global_move;
    
    // 迭代加深迴圈
    for (int current_depth = 1; current_depth <= 30; current_depth++) {
        std::vector<Move> moves = get_legal_actions_bitboard(board);
        
        // 將上一層的最佳步移到最前面，極大化剪枝效率
        if (current_depth > 1) {
            auto it = std::find(moves.begin(), moves.end(), best_global_move);
            if (it != moves.end()) std::rotate(moves.begin(), it, it + 1);
        }
        
        int alpha = -999999;
        int beta = 999999;
        Move best_move_this_depth;
        
        // 針對白方 (Max) 或黑方 (Min) 有不同的初始分數設定
        int best_score_this_depth = isWhite ? -999999 : 999999;

        // 展開第一層的合法步 (Root Node)
        for (Move m : moves) {
            Board nextBoard = makeMove(board, m);
            // 注意！下一層要換對手，所以傳入 !isWhite
            int score = standard_minimax(nextBoard, current_depth - 1, alpha, beta, !isWhite);
            
            if (isWhite) { // 白方追求最大值
                if (score > best_score_this_depth) {
                    best_score_this_depth = score;
                    best_move_this_depth = m;
                }
                alpha = std::max(alpha, score);
            } else { // 黑方追求最小值
                if (score < best_score_this_depth) {
                    best_score_this_depth = score;
                    best_move_this_depth = m;
                }
                beta = std::min(beta, score);
            }
            
            // 檢查是否超時，超時則放棄這層搜尋，直接回傳上一層的結果
            if (get_time_ms() - start_time > time_limit_ms) {
                return best_global_move;
            }
        }
        
        best_global_move = best_move_this_depth; // 更新全局最佳步
        
        // 如果找到必勝步 (將死)，可提早結束
        if (isWhite && best_score_this_depth > 90000) break;
        if (!isWhite && best_score_this_depth < -90000) break;
    }
    
    return best_global_move;
    }
    ```
---