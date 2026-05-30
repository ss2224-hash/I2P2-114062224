# MinitChess (5x6) 局勢評分函數 (State Value Function) 進階設計架構

在實作了材質分、位置分 (PST) 與機動性之後，單純的「加總」仍不足以讓 AI 具備真正的大局觀。本文件定義了四種現代西洋棋引擎必備的進階架構設計，能大幅提升 AI 的運算效率與戰略深度。

---

## 1. 核心技術：平滑階段過渡 (Tapered Evaluation)

**設計目的：** 解決同一個棋子在「中局」與「殘局」價值截然不同的問題。
* **中局 (Midgame)：** 國王必須躲藏；兵推進的威脅次之。
* **殘局 (Endgame)：** 當大子(后、車)減少，國王沒有被將死的危險，國王必須主動走到中央參戰；此時兵推進的價值極高。

**實作方式：**
準備兩套 PST 表格 (中局與殘局)。依據當前盤面上「剩餘大子的總價值」計算出一個 `Phase` (階段權重)，再透過線性插值 (Linear Interpolation) 平滑混合兩套分數。

```cpp
// 1. 定義遊戲階段最大值 (MAX_PHASE)
// MinitChess 初始陣容 (不含兵與王)：后(900) + 車(500) + 象(320) + 馬(300) = 2020/單方
// 雙方總和約為 4040。我們設定 MAX_PHASE 為 4000。
const int MAX_PHASE = 4000;

// 2. 計算當前 Phase
int calculate_phase(Board& b) {
    int total_material = 0;
    // 掃描盤面，加總雙方現存的 騎士、主教、城堡、皇后 的價值
    // 注意：不要把兵 (Pawn) 和王 (King) 算進去
    total_material += (白后數量 + 黑后數量) * 900;
    total_material += (白車數量 + 黑車數量) * 500;
    total_material += (白象數量 + 黑象數量) * 320;
    total_material += (白馬數量 + 黑馬數量) * 300;
    
    // 確保 phase 不會超過界線
    return std::min(total_material, MAX_PHASE); 
}

// 3. 混合分數公式
// mg_score 為中局總分, eg_score 為殘局總分
// 當 Phase=4000，完全採納 mg_score；當 Phase=0，完全採納 eg_score。
int final_score = (mg_score * phase + eg_score * (MAX_PHASE - phase)) / MAX_PHASE;
```

---

## 2. 效能優化：快速短路評估 (Lazy Evaluation)

**設計目的：** 在 10 秒與 4GB 的限制下，榨出最深的 Minimax 搜尋樹。
**邏輯：** 如果某個局面的「純材質分數」勝負已經極度懸殊（例如多了一個后與一個車），我們根本不需要浪費 CPU 週期去計算複雜的機動性、兵形或國王安全，直接回傳材質分數即可。

```cpp
int lazy_margin = 1200; // 設定為大於一個后的價值，並加上安全容錯空間

int material_score = calculate_basic_material(b);
if (material_score > lazy_margin || material_score < -lazy_margin) {
    return material_score; // 提早結束評估，極大幅度節省時間
}
```

---

## 3. 細節微調：先手優勢與子力協同

這些微調能解決 AI 遭遇對稱局面時的「猶豫不決」，並賦予其更像人類的攻擊性。

### 3.1 先手優勢 (Tempo Bonus)
* **概念：** 輪到自己走棋永遠是一種優勢（主動權）。
* **實作：** 在評估的最後，無條件給予**當前輪到走棋的玩家**微小加分（例如 `+15 分`）。

### 3.2 雙象優勢 (Bishop Pair)
* **概念：** 單隻象只能控制一半顏色的格子（黑格或白格）。同時擁有雙象能形成無死角的交叉火力。
* **實作：** 檢查陣列或 Bitboard，`if (己方象數量 >= 2) mg_score += 30; eg_score += 40;`。

---

## 4. State Value Function 完整程式碼架構

整合上述所有概念，這是你的 `evaluateBoard` 應該具備的最終結構：

```cpp
int evaluateBoard(Board& b) {
    int mg_score = 0; // 中局評估分
    int eg_score = 0; // 殘局評估分

    // --- 1. 基礎材質計算 ---
    int white_mat = calculate_material(b, WHITE);
    int black_mat = calculate_material(b, BLACK);
    int mat_diff = white_mat - black_mat; // 以白方視角為正
    
    mg_score += mat_diff;
    eg_score += mat_diff;

    // --- 2. 快速短路評估 (Lazy Evaluation) ---
    if (std::abs(mat_diff) > 1200) {
        // 將視角轉換給 Minimax/Negamax (若以輪到走棋方為正)
        return (b.turn == WHITE) ? mat_diff : -mat_diff; 
    }

    // --- 3. 雙階段細節評估 (遍歷盤面或 Bitboard) ---
    // 計算 PST (位置分)
    mg_score += evaluate_PST_Midgame(b);
    eg_score += evaluate_PST_Endgame(b);
    
    // 計算 兵形 (Pawn Structure) 與 國王安全 (King Safety)
    mg_score += evaluate_pawn_structure(b);
    eg_score += evaluate_pawn_structure(b); // 兵形在殘局同樣重要
    mg_score += evaluate_king_safety(b);    // 中局國王安全極重要
    // 殘局通常不需要扣國王安全分，因為國王要出來戰鬥

    // 計算 安全機動性 (Safe Mobility)
    mg_score += evaluate_safe_mobility(b);
    eg_score += evaluate_safe_mobility(b);

    // 雙象優勢
    if (has_bishop_pair(b, WHITE)) { mg_score += 30; eg_score += 40; }
    if (has_bishop_pair(b, BLACK)) { mg_score -= 30; eg_score -= 40; }

    // --- 4. 平滑階段過渡 (Tapered Evaluation) ---
    int phase = calculate_phase(b);
    int final_score = (mg_score * phase + eg_score * (MAX_PHASE - phase)) / MAX_PHASE;

    // --- 5. 先手優勢 (Tempo Bonus) ---
    if (b.turn == WHITE) {
        final_score += 15;
    } else {
        final_score -= 15;
    }

    // --- 6. 視角回傳 ---
    // 根據你實作 Minimax 的方式。
    // 如果是標準 Minimax (Max找正最大，Min找負最小)，永遠回傳白方視角分數。
    // 如果是 Negamax，則回傳當前玩家視角： return (b.turn == WHITE) ? final_score : -final_score;
    return final_score;
}
```

---

