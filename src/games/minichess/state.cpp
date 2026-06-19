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

static const int mg_value[7] = {0, 100, 500, 320, 330, 900, 20000};
static const int eg_value[7] = {0, 120, 500, 330, 340, 900, 20000};

static const int phase_weights[7] = {0, 0, 2, 1, 1, 4, 0};

static const int mg_pst[6][BOARD_H][BOARD_W] = {
    // Pawn
    {{  0,   0,   0,   0,   0},  
     { 50,  50,  50,  50,  50},  
     { 10,  10,  20,  10,  10},  
     {  5,   5,  10,   5,   5},  
     {  0,   0,   0,   0,   0},  
     {  0,   0,   0,   0,   0}}, 
    // Rook
    {{  0,   0,   5,   0,   0}, {  5,  10,  10,  10,   5}, { -5,   0,   0,   0,  -5},
     { -5,   0,   0,   0,  -5}, { -5,   0,   0,   0,  -5}, {  0,   0,   5,   0,   0}},
    // Knight
    {{-10,  -5,  -5,  -5, -10}, { -5,   0,   5,   0,  -5}, { -5,   5,  10,   5,  -5},
     { -5,   5,  10,   5,  -5}, { -5,   0,   5,   0,  -5}, {-10,  -5,  -5,  -5, -10}},
    // Bishop
    {{ -5,   0,   0,   0,  -5}, {  0,   5,   5,   5,   0}, {  0,   5,  10,   5,   0},
     {  0,   5,  10,   5,   0}, {  0,   5,   5,   5,   0}, { -5,   0,   0,   0,  -5}},
    // Queen
    {{ -5,  -5,  -5,  -5,  -5}, { -5,   0,   0,   0,  -5}, { -5,   0,   5,   0,  -5},
     { -5,   0,   5,   0,  -5}, { -5,   0,   0,   0,  -5}, { -5,  -5,  -5,  -5,  -5}},
    // King 
    {{-30, -30, -30, -30, -30}, {-30, -30, -30, -30, -30}, {-30, -30, -30, -30, -30},
     {-20, -20, -20, -20, -20}, { 10,  10,  -5,  10,  10}, { 20,  20,   0,  20,  20}},
};

static const int eg_pst[6][BOARD_H][BOARD_W] = {
    // Pawn 
    {{  0,   0,   0,   0,   0},  
     { 80,  80,  80,  80,  80},  
     { 40,  40,  50,  40,  40},  
     { 20,  20,  30,  20,  20},  
     { 10,  10,  10,  10,  10},  
     {  0,   0,   0,   0,   0}}, 
    // Rook
    {{  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0},
     {  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0}, {  0,   0,   0,   0,   0}},
    // Knight
    {{-10,  -5,  -5,  -5, -10}, { -5,   0,   0,   0,  -5}, { -5,   0,   5,   0,  -5},
     { -5,   0,   5,   0,  -5}, { -5,   0,   0,   0,  -5}, {-10,  -5,  -5,  -5, -10}},
    // Bishop
    {{ -5,   0,   0,   0,  -5}, {  0,   0,   0,   0,   0}, {  0,   0,   5,   0,   0},
     {  0,   0,   5,   0,   0}, {  0,   0,   0,   0,   0}, { -5,   0,   0,   0,  -5}},
    // Queen
    {{ -5,  -5,  -5,  -5,  -5}, { -5,   0,   0,   0,  -5}, { -5,   0,   5,   0,  -5},
     { -5,   0,   5,   0,  -5}, { -5,   0,   0,   0,  -5}, { -5,  -5,  -5,  -5,  -5}},
    // King 
    {{-20, -10, -10, -10, -20}, {-10,   5,  10,   5, -10}, {-10,  10,  20,  10, -10},
     {-10,  10,  20,  10, -10}, {-10,   5,  10,   5, -10}, {-20, -10, -10, -10, -20}},
};


/*============================================================
 * evaluate()
 *============================================================*/

int State::evaluate(
    bool use_kp_eval,
    bool use_mobility,
    const GameHistory* history
){
    (void)history; 
    (void)use_kp_eval; 
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

    int my_pawns_on_col[BOARD_W] = {0};
    int opp_pawns_on_col[BOARD_W] = {0};

    // 🌟 新增：紀錄雙方國王的位置，用於殘局追殺
    int my_kr = -1, my_kc = -1;
    int opp_kr = -1, opp_kc = -1;

    // 1. 掃描全局
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

                if (my_piece == 1) my_pawns_on_col[c]++; 
                if (my_piece == 6) { my_kr = r; my_kc = c; } // 找到我方國王
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
                if (opp_piece == 6) { opp_kr = r; opp_kc = c; } // 找到敵方國王
            }
        }
    }

    // 2. 兵陣結構與重子控制
    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            
            int my_piece = self_board[r][c];
            if (my_piece == 1) {
                if (my_pawns_on_col[c] > 1) {
                    my_mg -= 15; my_eg -= 20;
                }
                bool isolated = true;
                if (c > 0 && my_pawns_on_col[c - 1] > 0) isolated = false;
                if (c < BOARD_W - 1 && my_pawns_on_col[c + 1] > 0) isolated = false;
                if (isolated) {
                    my_mg -= 10; my_eg -= 20;
                }
                bool passed = true;
                int forward_dir = (this->player == 0) ? -1 : 1;
                for (int check_r = r + forward_dir; check_r >= 0 && check_r < BOARD_H; check_r += forward_dir) {
                    if (oppn_board[check_r][c]) passed = false;
                    if (c > 0 && oppn_board[check_r][c-1] == 1) passed = false;
                    if (c < BOARD_W - 1 && oppn_board[check_r][c+1] == 1) passed = false;
                }
                if (passed) {
                    my_mg += 30; my_eg += 80; 
                }
            }
            else if (my_piece == 2 || my_piece == 5) {
                if (my_pawns_on_col[c] == 0) { 
                    my_mg += 10; my_eg += 10;
                    if (opp_pawns_on_col[c] == 0) { 
                        my_mg += 15; my_eg += 15;
                    }
                }
            }

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

    // 🌟 3. 殘局追殺機制 (Mop-up Evaluation)
    // 只有在殘局 (phase 低) 且雙方國王都在場上時才啟動
    if (phase < 6 && my_kr != -1 && opp_kr != -1) {
        
        // 如果我方處於絕對優勢 (領先超過 300 分，約等於多一個輕子以上)
        if (my_eg - opp_eg > 300) {
            // A. 將對手國王逼迫到角落或邊緣 (曼哈頓距離計算)
            int center_r = BOARD_H / 2;
            int center_c = BOARD_W / 2;
            int opp_dist_to_center = std::abs(opp_kr - center_r) + std::abs(opp_kc - center_c);
            my_eg += opp_dist_to_center * 20; // 越靠近邊緣，加分越多

            // B. 自己的國王主動靠近對手國王
            int kings_dist = std::abs(my_kr - opp_kr) + std::abs(my_kc - opp_kc);
            my_eg += (14 - kings_dist) * 10; // 兩王距離越近，加分越多
        }
        // 如果是我方落後，反向操作：死命往中間跑，並遠離對手國王
        else if (opp_eg - my_eg > 300) {
            int center_r = BOARD_H / 2;
            int center_c = BOARD_W / 2;
            int my_dist_to_center = std::abs(my_kr - center_r) + std::abs(my_kc - center_c);
            opp_eg += my_dist_to_center * 20;

            int kings_dist = std::abs(my_kr - opp_kr) + std::abs(my_kc - opp_kc);
            opp_eg += (14 - kings_dist) * 10;
        }
    }

    // 4. 混合式過渡 (Tapered Eval Calculation)
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


/*============================================================
 * Dispatcher
 *============================================================*/
void State::get_legal_actions(){
    get_legal_actions_naive();
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

