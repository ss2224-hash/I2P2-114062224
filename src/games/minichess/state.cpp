#include <iostream>
#include <sstream>
#include <cstdint>
#include <cstdlib>

#include "./state.hpp"
#include "config.hpp"
#include "../../policy/game_history.hpp"

/*============================================================
 * 🌟 大師級評估系統 (Master-Level Evaluation)
 *============================================================*/

// 1. 子力價值分為：中局 (Midgame) 與殘局 (Endgame)
// 兵=0, 車=1, 馬=2, 象=3, 后=4, 王=5 (對應原本 1~6 的 piece ID)
static const int mg_value[7] = {0, 100, 500, 320, 330, 900, 20000};
static const int eg_value[7] = {0, 120, 500, 330, 340, 900, 20000};

// 棋子帶來的 "時期權重" (用來判斷現在是開局還是殘局)
// 滿分大約 24 (代表剛開局)，0 代表只剩下兵跟王
static const int phase_weights[7] = {0, 0, 2, 1, 1, 4, 0};

// 2. 漸進式位置表 (Tapered PSTs) - 從白方視角 (向 row=0 前進)
// 🌟 中局表 (Midgame PST)：國王要龜縮、小兵別亂衝
static const int mg_pst[6][BOARD_H][BOARD_W] = {
    // 兵 (Pawn)
    {{  0,   0,   0,   0,   0},  
     { 50,  50,  50,  50,  50},  
     { 10,  10,  20,  10,  10},  
     {  5,   5,  10,   5,   5},  
     {  0,   0,   0,   0,   0},  
     {  0,   0,   0,   0,   0}}, 
    // 車 (Rook)
    {{  0,   0,   5,   0,   0}, {  5,  10,  10,  10,   5}, { -5,   0,   0,   0,  -5},
     { -5,   0,   0,   0,  -5}, { -5,   0,   0,   0,  -5}, {  0,   0,   5,   0,   0}},
    // 馬 (Knight)
    {{-10,  -5,  -5,  -5, -10}, { -5,   0,   5,   0,  -5}, { -5,   5,  10,   5,  -5},
     { -5,   5,  10,   5,  -5}, { -5,   0,   5,   0,  -5}, {-10,  -5,  -5,  -5, -10}},
    // 象 (Bishop)
    {{ -5,   0,   0,   0,  -5}, {  0,   5,   5,   5,   0}, {  0,   5,  10,   5,   0},
     {  0,   5,  10,   5,   0}, {  0,   5,   5,   5,   0}, { -5,   0,   0,   0,  -5}},
    // 后 (Queen)
    {{ -5,  -5,  -5,  -5,  -5}, { -5,   0,   0,   0,  -5}, { -5,   0,   5,   0,  -5},
     { -5,   0,   5,   0,  -5}, { -5,   0,   0,   0,  -5}, { -5,  -5,  -5,  -5,  -5}},
    // 國王 (King) - 🌟 中局縮在底線角落！
    {{-30, -30, -30, -30, -30}, {-30, -30, -30, -30, -30}, {-30, -30, -30, -30, -30},
     {-20, -20, -20, -20, -20}, { 10,  10,  -5,  10,  10}, { 20,  20,   0,  20,  20}},
};

// 🌟 殘局表 (Endgame PST)：國王出擊！小兵瘋狂衝刺！
static const int eg_pst[6][BOARD_H][BOARD_W] = {
    // 兵 (Pawn) - 越接近升變分數飆越高
    {{  0,   0,   0,   0,   0},  
     { 80,  80,  80,  80,  80},  
     { 40,  40,  50,  40,  40},  
     { 20,  20,  30,  20,  20},  
     { 10,  10,  10,  10,  10},  
     {  0,   0,   0,   0,   0}}, 
    // 車 (Rook)
    {{  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0},
     {  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0}},
    // 馬 (Knight)
    {{-10,  -5,  -5,  -5, -10}, { -5,   0,   0,   0,  -5}, { -5,   0,   5,   0,  -5},
     { -5,   0,   5,   0,  -5}, { -5,   0,   0,   0,  -5}, {-10,  -5,  -5,  -5, -10}},
    // 象 (Bishop)
    {{ -5,   0,   0,   0,  -5}, {  0,   0,   0,   0,   0}, {  0,   0,   5,   0,   0},
     {  0,   0,   5,   0,   0}, {  0,   0,   0,   0,   0}, { -5,   0,   0,   0,  -5}},
    // 后 (Queen)
    {{ -5,  -5,  -5,  -5,  -5}, { -5,   0,   0,   0,  -5}, { -5,   0,   5,   0,  -5},
     { -5,   0,   5,   0,  -5}, { -5,   0,   0,   0,  -5}, { -5,  -5,  -5,  -5,  -5}},
    // 國王 (King) - 🌟 殘局站到棋盤中央主宰一切！
    {{-20, -10, -10, -10, -20}, {-10,   5,  10,   5, -10}, {-10,  10,  20,  10, -10},
     {-10,  10,  20,  10, -10}, {-10,   5,  10,   5, -10}, {-20, -10, -10, -10, -20}},
};


/*============================================================
 * evaluate() — 結合時期、兵陣與重子控制的現代評估
 *============================================================*/

int State::evaluate(
    bool use_kp_eval,
    bool use_mobility,
    const GameHistory* history
){
    (void)history; 
    (void)use_kp_eval; // 這次的升級將全面套用，不再區分簡單模式
    (void)use_mobility;

    if(this->game_state == WIN){
        return P_MAX - this->step; 
    }
    if(this->game_state == DRAW){
        return 0;
    }

    auto self_board = this->board.board[this->player];
    auto oppn_board = this->board.board[1 - this->player];
    
    int my_mg = 0, my_eg = 0;
    int opp_mg = 0, opp_eg = 0;
    int phase = 0;

    // 🌟 特徵收集器：為了極速計算兵陣與開放線
    int my_pawns_on_col[BOARD_W] = {0};
    int opp_pawns_on_col[BOARD_W] = {0};

    // 1. 掃描全局，計算子力、位置與收集兵陣資訊
    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            
            // 我方計算
            int my_piece = self_board[r][c];
            if(my_piece){
                phase += phase_weights[my_piece];
                my_mg += mg_value[my_piece];
                my_eg += eg_value[my_piece];
                
                int pst_r = (this->player == 0) ? r : (BOARD_H - 1 - r);
                my_mg += mg_pst[my_piece - 1][pst_r][c];
                my_eg += eg_pst[my_piece - 1][pst_r][c];

                if (my_piece == 1) my_pawns_on_col[c]++; // 記錄小兵位置
            }

            // 敵方計算
            int opp_piece = oppn_board[r][c];
            if(opp_piece){
                phase += phase_weights[opp_piece];
                opp_mg += mg_value[opp_piece];
                opp_eg += eg_value[opp_piece];
                
                int opp_pst_r = (this->player == 0) ? (BOARD_H - 1 - r) : r;
                opp_mg += mg_pst[opp_piece - 1][opp_pst_r][c];
                opp_eg += eg_pst[opp_piece - 1][opp_pst_r][c];

                if (opp_piece == 1) opp_pawns_on_col[c]++;
            }
        }
    }

    // 2. 兵陣結構與重子控制 (Pawn Structure & Open Files)
    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            
            int my_piece = self_board[r][c];
            if (my_piece == 1) {
                // 🌟 疊兵 (Doubled Pawns) 懲罰：同一排有大於1隻自己的兵
                if (my_pawns_on_col[c] > 1) {
                    my_mg -= 15; my_eg -= 20;
                }
                // 🌟 孤兵 (Isolated Pawn) 懲罰：左右兩排都沒有自己的兵
                bool isolated = true;
                if (c > 0 && my_pawns_on_col[c - 1] > 0) isolated = false;
                if (c < BOARD_W - 1 && my_pawns_on_col[c + 1] > 0) isolated = false;
                if (isolated) {
                    my_mg -= 10; my_eg -= 20;
                }
                // 🌟 通路兵 (Passed Pawn) 獎勵：前方與斜前方都沒有敵人的兵
                // (MinitChess 棋盤小，通路兵極度致命！)
                bool passed = true;
                int forward_dir = (this->player == 0) ? -1 : 1;
                for (int check_r = r + forward_dir; check_r >= 0 && check_r < BOARD_H; check_r += forward_dir) {
                    if (oppn_board[check_r][c]) passed = false;
                    if (c > 0 && oppn_board[check_r][c-1] == 1) passed = false;
                    if (c < BOARD_W - 1 && oppn_board[check_r][c+1] == 1) passed = false;
                }
                if (passed) {
                    my_mg += 30; my_eg += 80; // 殘局通路兵價值連城
                }
            }
            // 🌟 開放線上的車與后 (Rooks & Queens on Open Files)
            else if (my_piece == 2 || my_piece == 5) {
                if (my_pawns_on_col[c] == 0) { // 半開放線 (Semi-open file)
                    my_mg += 10; my_eg += 10;
                    if (opp_pawns_on_col[c] == 0) { // 全開放線 (Open file)
                        my_mg += 15; my_eg += 15;
                    }
                }
            }

            // --- 敵方的兵陣計算 (完全對稱的邏輯) ---
            int opp_piece = oppn_board[r][c];
            if (opp_piece == 1) {
                if (opp_pawns_on_col[c] > 1) {
                    opp_mg -= 15; opp_eg -= 20;
                }
                bool isolated = true;
                if (c > 0 && opp_pawns_on_col[c - 1] > 0) isolated = false;
                if (c < BOARD_W - 1 && opp_pawns_on_col[c + 1] > 0) isolated = false;
                if (isolated) {
                    opp_mg -= 10; opp_eg -= 20;
                }
                bool passed = true;
                int forward_dir = (this->player == 0) ? 1 : -1;
                for (int check_r = r + forward_dir; check_r >= 0 && check_r < BOARD_H; check_r += forward_dir) {
                    if (self_board[check_r][c]) passed = false;
                    if (c > 0 && self_board[check_r][c-1] == 1) passed = false;
                    if (c < BOARD_W - 1 && self_board[check_r][c+1] == 1) passed = false;
                }
                if (passed) {
                    opp_mg += 30; opp_eg += 80; 
                }
            }
            else if (opp_piece == 2 || opp_piece == 5) {
                if (opp_pawns_on_col[c] == 0) { 
                    opp_mg += 10; opp_eg += 10;
                    if (my_pawns_on_col[c] == 0) { 
                        opp_mg += 15; opp_eg += 15;
                    }
                }
            }
        }
    }

    // 3. 混合式過渡 (Tapered Eval Calculation)
    // 總 Phase 最大約 24，代表開局；Phase = 0 代表純殘局
    if (phase > 24) phase = 24;
    int mg_score = my_mg - opp_mg;
    int eg_score = my_eg - opp_eg;

    int final_score = (mg_score * phase + eg_score * (24 - phase)) / 24;

    return final_score;
}

/*============================================================
 * Zobrist hash for transposition table
 *============================================================*/
static uint64_t zobrist_piece[2][7][BOARD_H][BOARD_W];
static uint64_t zobrist_side;
static bool zobrist_ready = false;

static void init_zobrist(){
    uint64_t s = 0x7A35C9D1E4F02B68ULL;
    auto rand64 = [&s]() -> uint64_t {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s;
    };
    for(int p = 0; p < 2; p++){
        for(int t = 0; t < 7; t++){
            for(int r = 0; r < BOARD_H; r++){
                for(int c = 0; c < BOARD_W; c++){
                    zobrist_piece[p][t][r][c] = rand64();
                }
            }
        }
    }
    zobrist_side = rand64();
    zobrist_ready = true;
}

uint64_t State::compute_hash_full() const{
    if(!zobrist_ready){
        init_zobrist();
    }
    uint64_t h = 0;
    for(int p = 0; p < 2; p++){
        for(int r = 0; r < BOARD_H; r++){
            for(int c = 0; c < BOARD_W; c++){
                int piece = this->board.board[p][r][c];
                if(piece){
                    h ^= zobrist_piece[p][piece][r][c];
                }
            }
        }
    }
    if(this->player){
        h ^= zobrist_side;
    }
    return h;
}


State* State::next_state(const Move& move){
    if(!zobrist_ready){ init_zobrist(); }

    Board next = this->board;
    Point from = move.first, to = move.second;
    int p = this->player;
    int opp = 1 - p;

    int8_t orig_piece = next.board[p][from.first][from.second];
    int8_t moved = orig_piece;
    if(moved == 1 && (to.first==BOARD_H-1 || to.first==0)){
        moved = 5;
    }

    uint64_t h = this->hash();
    h ^= zobrist_side; 
    h ^= zobrist_piece[p][orig_piece][from.first][from.second];

    int8_t captured = next.board[opp][to.first][to.second];
    if(captured){
        h ^= zobrist_piece[opp][captured][to.first][to.second];
        next.board[opp][to.first][to.second] = 0;
    }

    h ^= zobrist_piece[p][moved][to.first][to.second];

    next.board[p][from.first][from.second] = 0;
    next.board[p][to.first][to.second] = moved;

    State* ns = new State(next, opp);
    ns->zobrist_hash = h;
    ns->zobrist_valid = true;
    ns->step = this->step + 1; // 更新步數
    return ns;
}

// 🌟 新增：零記憶體分配 (Zero-Allocation) 的狀態更新函數
void State::apply_move(const Move& move, State& next_s) const {
    if(!zobrist_ready){ init_zobrist(); }

    next_s.board = this->board;
    next_s.player = 1 - this->player;
    Point from = move.first, to = move.second;
    int p = this->player;
    int opp = 1 - p;

    int8_t orig_piece = next_s.board.board[p][from.first][from.second];
    int8_t moved = orig_piece;
    
    // promotion for pawn
    if(moved == 1 && (to.first==BOARD_H-1 || to.first==0)){
        moved = 5;
    }

    uint64_t h = this->hash();
    h ^= zobrist_side;
    h ^= zobrist_piece[p][orig_piece][from.first][from.second];

    int8_t captured = next_s.board.board[opp][to.first][to.second];
    if(captured){
        h ^= zobrist_piece[opp][captured][to.first][to.second];
        next_s.board.board[opp][to.first][to.second] = 0;
    }

    h ^= zobrist_piece[p][moved][to.first][to.second];

    next_s.board.board[p][from.first][from.second] = 0;
    next_s.board.board[p][to.first][to.second] = moved;

    next_s.zobrist_hash = h;
    next_s.zobrist_valid = true;
    next_s.game_state = UNKNOWN;
    next_s.step = this->step + 1; // 複製步數並加 1
    
    // 保留 legal_actions 的 capacity，避免重新分配
    next_s.legal_actions.clear(); 
}

static const int move_table_rook_bishop[8][7][2] = {
  {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}},
  {{0, -1}, {0, -2}, {0, -3}, {0, -4}, {0, -5}, {0, -6}, {0, -7}},
  {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}},
  {{-1, 0}, {-2, 0}, {-3, 0}, {-4, 0}, {-5, 0}, {-6, 0}, {-7, 0}},
  {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}},
  {{1, -1}, {2, -2}, {3, -3}, {4, -4}, {5, -5}, {6, -6}, {7, -7}},
  {{-1, 1}, {-2, 2}, {-3, 3}, {-4, 4}, {-5, 5}, {-6, 6}, {-7, 7}},
  {{-1, -1}, {-2, -2}, {-3, -3}, {-4, -4}, {-5, -5}, {-6, -6}, {-7, -7}},
};

static const int move_table_knight[8][2] = {
  { 2,  1}, { 2, -1}, {-2,  1}, {-2, -1},
  { 1,  2}, { 1, -2}, {-1,  2}, {-1, -2},
};
static const int move_table_king[8][2] = {
  {1, 0}, {0, 1}, {-1, 0}, {0, -1}, 
  {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};


/*============================================================
 * Naive move generation (array-based, branch-heavy)
 *============================================================*/
void State::get_legal_actions_naive(){
    this->game_state = NONE;
    std::vector<Move> all_actions;
    all_actions.reserve(64);
    auto self_board = this->board.board[this->player];
    auto oppn_board = this->board.board[1 - this->player];

    int now_piece, oppn_piece;
    for(int i=0; i<BOARD_H; i+=1){
        for(int j=0; j<BOARD_W; j+=1){
            if((now_piece=self_board[i][j])){
                switch(now_piece){
                    case 1: //pawn
                        if(this->player && i<BOARD_H-1){
                            //black
                            if(!oppn_board[i+1][j] && !self_board[i+1][j]){
                                all_actions.push_back(Move(Point(i, j), Point(i+1, j)));
                            }
                            if(j<BOARD_W-1 && (oppn_piece=oppn_board[i+1][j+1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i+1, j+1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                            if(j>0 && (oppn_piece=oppn_board[i+1][j-1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i+1, j-1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                        }else if(!this->player && i>0){
                            //white
                            if(!oppn_board[i-1][j] && !self_board[i-1][j]){
                                all_actions.push_back(Move(Point(i, j), Point(i-1, j)));
                            }
                            if(j<BOARD_W-1 && (oppn_piece=oppn_board[i-1][j+1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i-1, j+1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                            if(j>0 && (oppn_piece=oppn_board[i-1][j-1])>0){
                                all_actions.push_back(Move(Point(i, j), Point(i-1, j-1)));
                                if(oppn_piece==6){
                                    this->game_state = WIN;
                                    this->legal_actions = all_actions;
                                    return;
                                }
                            }
                        }
                        break;

                    case 2: //rook
                    case 4: //bishop
                    case 5: //queen
                        int st, end;
                        switch(now_piece){
                            case 2: st=0; end=4; break; //rook
                            case 4: st=4; end=8; break; //bishop
                            case 5: st=0; end=8; break; //queen
                            default: st=0; end=-1;
                        }
                        for(int part=st; part<end; part+=1){
                            auto move_list = move_table_rook_bishop[part];
                            for(int k=0; k<std::max(BOARD_H, BOARD_W); k+=1){
                                int p[2] = {move_list[k][0] + i, move_list[k][1] + j};

                                if(p[0]>=BOARD_H || p[0]<0 || p[1]>=BOARD_W || p[1]<0){
                                    break;
                                }
                                now_piece = self_board[p[0]][p[1]];
                                if(now_piece){
                                    break;
                                }

                                all_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));

                                oppn_piece = oppn_board[p[0]][p[1]];
                                if(oppn_piece){
                                    if(oppn_piece==6){
                                        this->game_state = WIN;
                                        this->legal_actions = all_actions;
                                        return;
                                    }else{
                                        break;
                                    }
                                };
                            }
                        }
                        break;

                    case 3: //knight
                        for(auto move: move_table_knight){
                            int p[2] = {move[0] + i, move[1] + j};

                            if(p[0]>=BOARD_H || p[0]<0 || p[1]>=BOARD_W || p[1]<0){
                                continue;
                            }
                            now_piece = self_board[p[0]][p[1]];
                            if(now_piece){
                                continue;
                            }

                            all_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));

                            oppn_piece = oppn_board[p[0]][p[1]];
                            if(oppn_piece==6){
                                this->game_state = WIN;
                                this->legal_actions.clear();
                                this->legal_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));
                                return;
                            }
                        }
                        break;

                    case 6: //king
                        for(auto move: move_table_king){
                            int p[2] = {move[0] + i, move[1] + j};

                            if(p[0]>=BOARD_H || p[0]<0 || p[1]>=BOARD_W || p[1]<0){
                                continue;
                            }
                            now_piece = self_board[p[0]][p[1]];
                            if(now_piece){
                                continue;
                            }

                            all_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));

                            oppn_piece = oppn_board[p[0]][p[1]];
                            if(oppn_piece==6){
                                this->game_state = WIN;
                                
                                this->legal_actions.clear();
                                this->legal_actions.push_back(Move(Point(i, j), Point(p[0], p[1])));
                                return;
                            }
                        }
                        break;
                }
            }
        }
    }
    this->legal_actions = all_actions;
}


/*============================================================
 * Bitboard move generation
 *============================================================*/
#define BB_SQ(r, c)  ((r) * BOARD_W + (c))
#define BB_ROW(sq)   ((sq) / BOARD_W)
#define BB_COL(sq)   ((sq) % BOARD_W)

// Precomputed attack tables (initialized once)
static uint32_t bb_knight[30];       // knight attack mask per square
static uint32_t bb_king[30];         // king attack mask per square
static uint32_t bb_pawn_push[2][30]; // pawn push target per player/square
static uint32_t bb_pawn_cap[2][30];  // pawn capture targets per player/square
static bool bb_ready = false;

// Sliding piece direction vectors (0-3: rook, 4-7: bishop, 0-7: queen)
static const int bb_dr[8] = {0, 0, 1, -1, 1, 1, -1, -1};
static const int bb_dc[8] = {1, -1, 0, 0, 1, -1, 1, -1};

static void bb_init(){
    static const int kn_dr[8] = {1, 1, -1, -1, 2, 2, -2, -2};
    static const int kn_dc[8] = {2, -2, 2, -2, 1, -1, 1, -1};
    static const int ki_dr[8] = {1, 0, -1, 0, 1, 1, -1, -1};
    static const int ki_dc[8] = {0, 1, 0, -1, 1, -1, 1, -1};

    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            int sq = BB_SQ(r, c);

            // Knight
            bb_knight[sq] = 0;
            for(int d = 0; d < 8; d++){
                int nr = r + kn_dr[d], nc = c + kn_dc[d];
                if(nr >= 0 && nr < BOARD_H && nc >= 0 && nc < BOARD_W){
                    bb_knight[sq] |= 1u << BB_SQ(nr, nc);
                }
            }

            // King
            bb_king[sq] = 0;
            for(int d = 0; d < 8; d++){
                int nr = r + ki_dr[d], nc = c + ki_dc[d];
                if(nr >= 0 && nr < BOARD_H && nc >= 0 && nc < BOARD_W){
                    bb_king[sq] |= 1u << BB_SQ(nr, nc);
                }
            }

            // Pawn (player 0 = white, advances up = row-1)
            bb_pawn_push[0][sq] = 0;
            bb_pawn_cap[0][sq] = 0;
            if(r > 0){
                bb_pawn_push[0][sq] = 1u << BB_SQ(r-1, c);
                if(c > 0){
                    bb_pawn_cap[0][sq] |= 1u << BB_SQ(r-1, c-1);
                }
                if(c < BOARD_W-1){
                    bb_pawn_cap[0][sq] |= 1u << BB_SQ(r-1, c+1);
                }
            }

            // Pawn (player 1 = black, advances down = row+1)
            bb_pawn_push[1][sq] = 0;
            bb_pawn_cap[1][sq] = 0;
            if(r < BOARD_H-1){
                bb_pawn_push[1][sq] = 1u << BB_SQ(r+1, c);
                if(c > 0){
                    bb_pawn_cap[1][sq] |= 1u << BB_SQ(r+1, c-1);
                }
                if(c < BOARD_W-1){
                    bb_pawn_cap[1][sq] |= 1u << BB_SQ(r+1, c+1);
                }
            }
        }
    }
    bb_ready = true;
}

void State::get_legal_actions_bitboard(){
    if(!bb_ready){
        bb_init();
    }

    this->game_state = NONE;
    this->legal_actions.clear();
    this->legal_actions.reserve(64);

    int self = this->player;
    int oppn = 1 - self;

    // Build occupancy bitmasks and piece-type lookup
    uint32_t self_occ = 0, oppn_occ = 0;
    int self_pt[30] = {};  
    int oppn_pt[30] = {};  

    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            int sq = BB_SQ(r, c);
            if(this->board.board[self][r][c]){
                self_occ |= 1u << sq;
                self_pt[sq] = this->board.board[self][r][c];
            }
            if(this->board.board[oppn][r][c]){
                oppn_occ |= 1u << sq;
                oppn_pt[sq] = this->board.board[oppn][r][c];
            }
        }
    }

    uint32_t all_occ = self_occ | oppn_occ;

    // Iterate own pieces via bit scan
    uint32_t pieces = self_occ;
    while(pieces){
        int sq = __builtin_ctz(pieces);
        pieces &= pieces - 1;
        int r = BB_ROW(sq), c = BB_COL(sq);
        int piece = self_pt[sq];
        uint32_t targets = 0;

        switch(piece){
            case 1: { // Pawn
                uint32_t push = bb_pawn_push[self][sq] & ~all_occ;
                uint32_t cap = bb_pawn_cap[self][sq] & oppn_occ;
                uint32_t cap_scan = cap;
                while(cap_scan){
                    int to = __builtin_ctz(cap_scan);
                    cap_scan &= cap_scan - 1;
                    if(oppn_pt[to] == 6){
                        this->game_state = WIN;
                        this->legal_actions.push_back(
                            Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
                        return;
                    }
                }
                targets = push | cap;
                break;
            }

            case 3: { // Knight
                targets = bb_knight[sq] & ~self_occ;
                uint32_t opp_targets = targets & oppn_occ;
                while(opp_targets){
                    int to = __builtin_ctz(opp_targets);
                    opp_targets &= opp_targets - 1;
                    if(oppn_pt[to] == 6){
                        this->game_state = WIN;
                        this->legal_actions.push_back(
                            Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
                        return;
                    }
                }
                break;
            }

            case 6: { // King
                targets = bb_king[sq] & ~self_occ;
                uint32_t opp_targets = targets & oppn_occ;
                while(opp_targets){
                    int to = __builtin_ctz(opp_targets);
                    opp_targets &= opp_targets - 1;
                    if(oppn_pt[to] == 6){
                        this->game_state = WIN;
                        this->legal_actions.push_back(
                            Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
                        return;
                    }
                }
                break;
            }

            case 2: // Rook
            case 4: // Bishop
            case 5: { // Queen
                int d_start = (piece == 4) ? 4 : 0;
                int d_end   = (piece == 2) ? 4 : 8;
                for(int d = d_start; d < d_end; d++){
                    int cr = r + bb_dr[d], cc = c + bb_dc[d];
                    while(cr >= 0 && cr < BOARD_H && cc >= 0 && cc < BOARD_W){
                        int to = BB_SQ(cr, cc);
                        uint32_t to_bit = 1u << to;
                        if(self_occ & to_bit){
                            break; 
                        }

                        if((oppn_occ & to_bit) && oppn_pt[to] == 6){
                            this->game_state = WIN;
                            this->legal_actions.push_back(
                                Move(Point(r, c), Point(cr, cc)));
                            return;
                        }

                        targets |= to_bit;
                        if(oppn_occ & to_bit){
                            break; 
                        }
                        cr += bb_dr[d]; cc += bb_dc[d];
                    }
                }
                break;
            }
        }

        while(targets){
            int to = __builtin_ctz(targets);
            targets &= targets - 1;
            this->legal_actions.push_back(
                Move(Point(r, c), Point(BB_ROW(to), BB_COL(to))));
        }
    }
}


/*============================================================
 * Dispatcher
 *============================================================*/
void State::get_legal_actions(){
    #ifdef USE_BITBOARD
    get_legal_actions_bitboard();
    #else
    get_legal_actions_naive();
    #endif
}


const char piece_table[2][7][5] = {
  {" ", "♙", "♖", "♘", "♗", "♕", "♔"},
  {" ", "♟", "♜", "♞", "♝", "♛", "♚"}
};

std::string State::encode_output() const{
    std::stringstream ss;
    int now_piece;
    for(int i=0; i<BOARD_H; i+=1){
        for(int j=0; j<BOARD_W; j+=1){
            if((now_piece = this->board.board[0][i][j])){
                ss << std::string(piece_table[0][now_piece]);
            }else if((now_piece = this->board.board[1][i][j])){
                ss << std::string(piece_table[1][now_piece]);
            }else{
                ss << " ";
            }
            ss << " ";
        }
        ss << "\n";
    }
    return ss.str();
}

std::string State::encode_state(){
    std::stringstream ss;
    ss << this->player;
    ss << "\n";
    for(int pl=0; pl<2; pl+=1){
        for(int i=0; i<BOARD_H; i+=1){
            for(int j=0; j<BOARD_W; j+=1){
                ss << int(this->board.board[pl][i][j]);
                ss << " ";
            }
            ss << "\n";
        }
        ss << "\n";
    }
    return ss.str();
}


BaseState* State::create_null_state() const{
    State* s = new State(this->board, 1 - this->player);
    s->get_legal_actions();
    return s;
}


/* === Board serialization === */
static const char* piece_chars = ".PRNBQK";
static const char* piece_chars_lower = ".prnbqk";

std::string State::encode_board() const{
    std::string s;
    for(int r = 0; r < BOARD_H; r++){
        if(r > 0){
            s += '/';
        }
        for(int c = 0; c < BOARD_W; c++){
            int w = board.board[0][r][c];
            int b = board.board[1][r][c];
            if(w > 0 && w <= 6){
                s += piece_chars[w];
            }else if(b > 0 && b <= 6){
                s += piece_chars_lower[b];
            }else{
                s += '.';
            }
        }
    }
    return s;
}

void State::decode_board(const std::string& s, int side_to_move){
    player = side_to_move;
    game_state = UNKNOWN;
    zobrist_valid = false;
    board = Board{};
    int r = 0, c = 0;
    for(char ch : s){
        if(ch == '/'){
            r++;
            c = 0;
            continue;
        }
        if(r >= BOARD_H || c >= BOARD_W){
            break;
        }
        if(ch >= 'A' && ch <= 'Z'){
            for(int p = 1; p <= 6; p++){
                if(piece_chars[p] == ch){
                    board.board[0][r][c] = p;
                    break;
                }
            }
        }else if(ch >= 'a' && ch <= 'z'){
            for(int p = 1; p <= 6; p++){
                if(piece_chars_lower[p] == ch){
                    board.board[1][r][c] = p;
                    break;
                }
            }
        }
        c++;
    }
    get_legal_actions();
}

std::string State::cell_display(int row, int col) const{
    int w = static_cast<int>(board.board[0][row][col]);
    int b = static_cast<int>(board.board[1][row][col]);
    if(w){
        const char* names = ".PRNBQK";
        return std::string(" ") + names[w] + " ";
    }else if(b){
        const char* names = ".prnbqk";
        return std::string(" ") + names[b] + " ";
    }else{
        return " . ";
    }
}

/* === Repetition: chess 3-fold rule === */
bool State::check_repetition(const GameHistory& history, int& out_score) const {
    if(history.count(hash()) >= 3){
        out_score = 0;  /* draw */
        return true;
    }
    return false;
}

