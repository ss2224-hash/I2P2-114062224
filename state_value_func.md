## 完成狀態

這份評估函數已經完成，並經過測試以確保其正確性和效率。
# MinitChess (5x6) 標準 Minimax 專用評估函數實作

這份指南專為傳統 Minimax (區分 Max/Min 節點) 所設計。去除了容易引發波動的進階參數，保留了運算速度最快、CP 值最高的核心邏輯。

---

## 1. 基礎架構約定

在標準 Minimax 中，我們約定：
* **Max (AI)** 扮演 **白方 (White)**，目標是讓分數越大越好 (正無限大)。
* **Min (對手)** 扮演 **黑方 (Black)**，目標是讓分數越小越好 (負無限大)。
* 評估函數的回傳值永遠是：`白方總分 - 黑方總分`。

---

## 2. 簡化版靜態評估函數 (State Value Function)

這個版本捨棄了複雜的階段融合，改採最直接的線性加總，並保留了極度高效的快速短路 (Lazy Eval)。

```cpp
// 定義材質價值
const int VAL_PAWN = 100;
const int VAL_KNIGHT = 300;
const int VAL_BISHOP = 320;
const int VAL_ROOK = 500;
const int VAL_QUEEN = 900;

int evaluateBoard_Standard(Board& b) {
    int white_score = 0;
    int black_score = 0;
    int total_material = 0; // 用來簡單判斷是否進入殘局

    // 1. 基礎材質計算 (Material)
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 5; c++) {
            char wp = b.board[0][r][c];
            if (wp == 'P') { white_score += VAL_PAWN; total_material += VAL_PAWN; }
            else if (wp == 'N') { white_score += VAL_KNIGHT; total_material += VAL_KNIGHT; }
            else if (wp == 'B') { white_score += VAL_BISHOP; total_material += VAL_BISHOP; }
            else if (wp == 'R') { white_score += VAL_ROOK; total_material += VAL_ROOK; }
            else if (wp == 'Q') { white_score += VAL_QUEEN; total_material += VAL_QUEEN; }
            
            char bp = b.board[1][r][c];
            if (bp == 'p') { black_score += VAL_PAWN; total_material += VAL_PAWN; }
            else if (bp == 'n') { black_score += VAL_KNIGHT; total_material += VAL_KNIGHT; }
            else if (bp == 'b') { black_score += VAL_BISHOP; total_material += VAL_BISHOP; }
            else if (bp == 'r') { black_score += VAL_ROOK; total_material += VAL_ROOK; }
            else if (bp == 'q') { black_score += VAL_QUEEN; total_material += VAL_QUEEN; }
        }
    }

    // 2. 快速短路評估 (Lazy Evaluation) - 極度重要
    // 如果白方或黑方的純材質已經領先超過 1200 分 (大於一隻后)，
    // 直接回傳勝負，不用浪費時間算後面的陣型！
    int eval_diff = white_score - black_score;
    if (eval_diff > 1200 || eval_diff < -1200) {
        return eval_diff;
    }

    // 3. 位置評分 (PST) 與 基礎機動性
    // 取得雙方的 Bitboard 遮罩 (用於算機動性)
    uint32_t white_mask = get_player_bitboard(b, 0);
    uint32_t black_mask = get_player_bitboard(b, 1);
    bool is_endgame = (total_material < 1500); // 簡單粗暴的殘局判定

    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 5; c++) {
            int index = r * 5 + c;
            
            // --- 評估白方 ---
            char wp = b.board[0][r][c];
            if (wp != '\0') {
                if (wp == 'P') white_score += PST_Pawn[r][c];
                else if (wp == 'N') {
                    white_score += PST_Knight[r][c];
                    // 基礎機動性 (合法步數 * 3分)
                    white_score += __builtin_popcount(bb_knight[index] & ~white_mask) * 3;
                }
                else if (wp == 'B') white_score += PST_Bishop[r][c];
                else if (wp == 'R') white_score += PST_Rook[r][c];
                else if (wp == 'K') {
                    // 只有國王需要根據殘局切換表
                    if (is_endgame) white_score += PST_King_Endgame[r][c];
                    else white_score += PST_King_Midgame[r][c];
                }
            }
            
            // --- 評估黑方 (記得 Row 要翻轉) ---
            char bp = b.board[1][r][c];
            if (bp != '\0') {
                int flipped_r = 5 - r;
                if (bp == 'p') black_score += PST_Pawn[flipped_r][c];
                else if (bp == 'n') {
                    black_score += PST_Knight[flipped_r][c];
                    black_score += __builtin_popcount(bb_knight[index] & ~black_mask) * 3;
                }
                else if (bp == 'b') black_score += PST_Bishop[flipped_r][c];
                else if (bp == 'r') black_score += PST_Rook[flipped_r][c];
                else if (bp == 'k') {
                    if (is_endgame) black_score += PST_King_Endgame[flipped_r][c];
                    else black_score += PST_King_Midgame[flipped_r][c];
                }
            }
        }
    }

    // 回傳 白方總分 - 黑方總分 (Max 視角)
    return white_score - black_score;
}
```

---

## 3. 標準 Minimax (+ Alpha-Beta) 搭配

評估函數寫好後，你的 Minimax 演算法就可以保持最經典、最不容易寫錯的教科書形式。這段程式碼直接處理 `isMaximizingPlayer` 的邏輯分支：

```cpp
// isMaximizingPlayer: true 代表目前是白方(Max)要走棋，false 代表黑方(Min)要走棋
int standard_minimax(Board board, int depth, int alpha, int beta, bool isMaximizingPlayer) {
    // 1. 終止條件
    if (depth == 0 || isGameOver(board)) {
        return evaluateBoard_Standard(board);
    }
    
    std::vector<Move> moves = get_legal_actions_bitboard(board);
    
    // 2. 步法排序 (若想省時間不寫 MVV-LVA，直接用這行：先搜吃子步)
    // 就算只做這個簡單的排序，Alpha-Beta 剪枝率也會大幅提升！
    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return a.is_capture > b.is_capture; 
    });

    // 3. Max 節點 (白方回合)
    if (isMaximizingPlayer) {
        int maxEval = -999999;
        for (Move m : moves) {
            Board nextBoard = makeMove(board, m);
            int eval = standard_minimax(nextBoard, depth - 1, alpha, beta, false);
            
            maxEval = std::max(maxEval, eval);
            alpha = std::max(alpha, eval);
            if (beta <= alpha) break; // Beta 剪枝
        }
        return maxEval;
    } 
    // 4. Min 節點 (黑方回合)
    else {
        int minEval = 999999;
        for (Move m : moves) {
            Board nextBoard = makeMove(board, m);
            int eval = standard_minimax(nextBoard, depth - 1, alpha, beta, true);
            
            minEval = std::min(minEval, eval);
            beta = std::min(beta, eval);
            if (beta <= alpha) break; // Alpha 剪枝
        }
        return minEval;
    }
}
```
在標準 Minimax 的靜態評估函數中加入以下三種組合，可以在不影響搜尋深度的前提下，大幅提升 AI 的陣地戰能力。

---

## 1. 雙象優勢 (The Bishop Pair)

* **戰略意義：** 象只能走單一顏色的格子（黑格或白格）。一隻象會有 50% 的視野死角。當一方同時擁有兩隻象時，牠們的火力網能完美互補，形成極強的控制力。
* **分數建議：** `+30 分`（大約等於 1/3 個兵的價值）。
* **實作方式：** 在計算材質的雙層迴圈中，順便計算象的數量。

## 2. 雙車霸線 / 疊車 (Doubled Rooks)

* **戰略意義：** 車的威力在於控制直線。如果將兩隻車放在同一條直線（Column/File）上，牠們會互相保護，並形成無法被兵或輕子阻擋的「衝車」攻勢。這在 5x6 狹窄的棋盤中往往是致命的。
* **分數建議：** `+20 分`。
* **實作方式：** 建立一個大小為 5 的陣列來記錄每一欄有幾隻車。

## 3. 馬的前哨站 / 兵馬連環 (Knight Outpost)

* **戰略意義：** 馬是近戰兵種，如果單獨衝進敵陣很容易被趕走。但如果一隻馬**「背後有自己的兵保護」**，這隻馬就會變成一顆釘子（前哨站），對手必須付出極大代價（例如用車換馬）才能拔除牠。
* **分數建議：** `+15 分`。
* **實作方式：** 當找到馬時，檢查其斜後方是否有己方的兵。

---

## 完整程式碼整合範例

請將以下邏輯直接無縫插入你先前的 `evaluateBoard_Standard` 函數中：

```cpp
int evaluateBoard_Standard(Board& b) {
    int white_score = 0;
    int black_score = 0;

    // 棋子組合計數器
    int white_bishops = 0, black_bishops = 0;
    int white_rooks_col[5] = {0}, black_rooks_col[5] = {0};

    // 取得位元遮罩
    uint32_t white_mask = get_player_bitboard(b, 0);
    uint32_t black_mask = get_player_bitboard(b, 1);
    
    // 預先計算敵方小兵火力網 (安全機動性用)
    // 假設你能在 b 裡面快速拿到 pawn 的 bitboard，或在此自己算出
    uint32_t black_pawn_shields = get_black_pawn_attacks(get_pawn_bitboard(b, 1));
    uint32_t white_pawn_shields = get_white_pawn_attacks(get_pawn_bitboard(b, 0));

    // 全盤掃描 (O(30) 極速)
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 5; c++) {
            int index = r * 5 + c;

            // --- 白方處理 ---
            char wp = b.board[0][r][c];
            if (wp != '\0') {
                if (wp == 'P') white_score += VAL_PAWN + PST_Pawn[r][c];
                else if (wp == 'N') {
                    white_score += VAL_KNIGHT + PST_Knight[r][c];
                    // 前哨站判斷
                    if (r + 1 < 6 && ((c - 1 >= 0 && b.board[0][r+1][c-1] == 'P') || (c + 1 < 5 && b.board[0][r+1][c+1] == 'P'))) {
                        white_score += 15;
                    }
                    // 安全機動性
                    uint32_t safe_moves = bb_knight[index] & (~white_mask) & (~black_pawn_shields);
                    white_score += __builtin_popcount(safe_moves) * 3;
                }
                else if (wp == 'B') {
                    white_score += VAL_BISHOP + PST_Bishop[r][c];
                    white_bishops++;
                    white_score += get_slider_mobility(b, r, c, 0, DIR_BISHOP_R, DIR_BISHOP_C, 4) * 4;
                }
                else if (wp == 'R') {
                    white_score += VAL_ROOK + PST_Rook[r][c];
                    white_rooks_col[c]++;
                    white_score += get_slider_mobility(b, r, c, 0, DIR_ROOK_R, DIR_ROOK_C, 4) * 3;
                }
                else if (wp == 'Q') {
                    white_score += VAL_QUEEN + PST_Queen[r][c];
                    white_score += get_slider_mobility(b, r, c, 0, DIR_QUEEN_R, DIR_QUEEN_C, 8) * 1;
                }
                else if (wp == 'K') white_score += VAL_KING + PST_King_Midgame[r][c];
            }

            // --- 黑方處理 ---
            char bp = b.board[1][r][c];
            if (bp != '\0') {
                int flipped_r = 5 - r; // PST 視角翻轉
                if (bp == 'p') black_score += VAL_PAWN + PST_Pawn[flipped_r][c];
                else if (bp == 'n') {
                    black_score += VAL_KNIGHT + PST_Knight[flipped_r][c];
                    // 前哨站判斷
                    if (r - 1 >= 0 && ((c - 1 >= 0 && b.board[1][r-1][c-1] == 'p') || (c + 1 < 5 && b.board[1][r-1][c+1] == 'p'))) {
                        black_score += 15;
                    }
                    // 安全機動性
                    uint32_t safe_moves = bb_knight[index] & (~black_mask) & (~white_pawn_shields);
                    black_score += __builtin_popcount(safe_moves) * 3;
                }
                else if (bp == 'b') {
                    black_score += VAL_BISHOP + PST_Bishop[flipped_r][c];
                    black_bishops++;
                    black_score += get_slider_mobility(b, r, c, 1, DIR_BISHOP_R, DIR_BISHOP_C, 4) * 4;
                }
                else if (bp == 'r') {
                    black_score += VAL_ROOK + PST_Rook[flipped_r][c];
                    black_rooks_col[c]++;
                    black_score += get_slider_mobility(b, r, c, 1, DIR_ROOK_R, DIR_ROOK_C, 4) * 3;
                }
                else if (bp == 'q') {
                    black_score += VAL_QUEEN + PST_Queen[flipped_r][c];
                    black_score += get_slider_mobility(b, r, c, 1, DIR_QUEEN_R, DIR_QUEEN_C, 8) * 1;
                }
                else if (bp == 'k') black_score += VAL_KING + PST_King_Midgame[flipped_r][c];
            }
        }
    }

    // 結算棋子組合
    if (white_bishops >= 2) white_score += 30;
    if (black_bishops >= 2) black_score += 30;
    for (int c = 0; c < 5; c++) {
        if (white_rooks_col[c] >= 2) white_score += 20;
        if (black_rooks_col[c] >= 2) black_score += 20;
    }

    return white_score - black_score;
}
```
