#include <utility>
#include <algorithm>
#include <vector>
#include <cstring>
#include "state.hpp"
#include "minimax.hpp"

// ============================================================
// 🌟 核心引擎資料區
// ============================================================
namespace {
    enum TTFlag { TT_EXACT, TT_LOWERBOUND, TT_UPPERBOUND };
    
    struct TTEntry {
        uint64_t hash = 0;
        int depth = -1;
        int score = 0;
        TTFlag flag = TT_EXACT;
    };

    const size_t TT_SIZE = 1048576; 
    const uint64_t TT_MASK = TT_SIZE - 1;
    std::vector<TTEntry> transposition_table(TT_SIZE);

    std::vector<State> state_stack(128);

    // 安全有效的排序外掛：殺手步與歷史步記憶體
    Move killer_moves[128][2];
    int history_table[2][6][5][6][5];

    // 🌟 終極步法排序函數 (安全版)
    int score_action(State* state, const Move& action, int ply) {
        int current_player = state->player;
        int opp = 1 - current_player;
        
        int from_r = action.first.first;   int from_c = action.first.second;
        int to_r   = action.second.first;  int to_c   = action.second.second;

        int8_t attacker = state->board.board[current_player][from_r][from_c];
        int8_t victim = state->board.board[opp][to_r][to_c];
        
        // 1. 最高優先：吃子步 (MVV-LVA)
        if (victim != 0) {
            static const int val[7] = {0, 10, 30, 30, 50, 90, 1000};
            return 10000 + val[victim] * 10 - val[attacker]; 
        }

        // 2. 次高優先：殺手步 (Killer Moves)
        if (ply < 128) {
            if (action == killer_moves[ply][0]) return 9000;
            if (action == killer_moves[ply][1]) return 8000;
        }

        // 3. 一般優先：歷史啟發 (History Heuristic)
        return history_table[current_player][from_r][from_c][to_r][to_c]; 
    }

    // 🌟 靜止搜尋 (Quiescence Search)
    int quiescence_search(State *state, GameHistory& history, SearchContext& ctx, const MMParams& p, int alpha, int beta, int ply) {
        ctx.nodes++;
        if (ctx.stop) return 0;
        
        // =========================================================
        // 🌟 新增防線：百步末日極限判斷 (Horizon Fix)
        // 如果到了第 100 步，遊戲強制結束，直接結算子力！
        // =========================================================
        if (state->step >= 100) {
            int my_mat = 0, opp_mat = 0;
            static const int mat_val[7] = {0, 100, 500, 320, 330, 900, 20000};
            for(int r = 0; r < BOARD_H; r++){
                for(int c = 0; c < BOARD_W; c++){
                    my_mat += mat_val[state->board.board[state->player][r][c]];
                    opp_mat += mat_val[state->board.board[1 - state->player][r][c]];
                }
            }
            if (my_mat > opp_mat) return (P_MAX - 200) - state->step; // 判定為獲勝
            if (my_mat < opp_mat) return -(P_MAX - 200) + state->step; // 判定為戰敗
            return 0; // 平局
        }
        // =========================================================

        int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        if (stand_pat >= beta) return beta;
        if (alpha < stand_pat) alpha = stand_pat;

        if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
            state->get_legal_actions();
        }

        // 回歸安全穩定的 std::sort
        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const auto& a, const auto& b) {
            return score_action(state, a, ply) > score_action(state, b, ply);
        });

        if ((size_t)ply >= state_stack.size()) state_stack.resize(ply + 2);

        for (auto& action : state->legal_actions) {
            // QS 僅搜尋吃子步
            int opp = 1 - state->player;
            if (state->board.board[opp][action.second.first][action.second.second] == 0) continue; 

            State* next = &state_stack[ply];
            state->apply_move(action, *next);

            int score = -quiescence_search(next, history, ctx, p, -beta, -alpha, ply + 1);

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }
}


/*============================================================
 * MiniMax — eval_ctx (移除 NMP 與 LMR，回歸純粹 PVS)
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p,
    int alpha,
    int beta
){
    ctx.nodes++;
    if(ply > ctx.seldepth) ctx.seldepth = ply;
    if(ctx.stop) return 0;

    uint64_t hash_key = state->hash();

    TTEntry& tt_entry = transposition_table[hash_key & TT_MASK];
    if (tt_entry.hash == hash_key && tt_entry.depth >= depth) {
        if (tt_entry.flag == TT_EXACT) return tt_entry.score;
        if (tt_entry.flag == TT_LOWERBOUND && tt_entry.score >= beta) return tt_entry.score;
        if (tt_entry.flag == TT_UPPERBOUND && tt_entry.score <= alpha) return tt_entry.score;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == DRAW) return 0;
    if(state->game_state == WIN) return P_MAX - state->step; 

    // =========================================================
    // 🌟 新增防線：百步末日極限判斷 (Horizon Fix)
    // =========================================================
    if (state->step >= 100) {
        int my_mat = 0, opp_mat = 0;
        static const int mat_val[7] = {0, 100, 500, 320, 330, 900, 20000};
        for(int r = 0; r < BOARD_H; r++){
            for(int c = 0; c < BOARD_W; c++){
                my_mat += mat_val[state->board.board[state->player][r][c]];
                opp_mat += mat_val[state->board.board[1 - state->player][r][c]];
            }
        }
        // 賦予極端分數：AI 會為了這個「技術性勝利」不擇手段
        if (my_mat > opp_mat) return (P_MAX - 200) - state->step; 
        if (my_mat < opp_mat) return -(P_MAX - 200) + state->step; 
        return 0; 
    }
    // =========================================================

    int rep_score;
    if(state->check_repetition(history, rep_score)) return rep_score;
    history.push(hash_key);

    if(depth <= 0){
        int qs_score = quiescence_search(state, history, ctx, p, alpha, beta, ply);
        history.pop(hash_key);
        return qs_score;
    }

    // 回歸安全穩定的 std::sort
    std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const auto& a, const auto& b) {
        return score_action(state, a, ply) > score_action(state, b, ply);
    });

    if ((size_t)ply >= state_stack.size()) state_stack.resize(ply + 2);

    int best_score = M_MAX;
    int original_alpha = alpha;
    bool first_move = true;

    for(auto& action : state->legal_actions){
        State* next = &state_stack[ply];
        state->apply_move(action, *next);

        int score;

        if (first_move) {
            // 第 1 步：全視窗正常搜尋 (PVS 主變例)
            int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -beta, -alpha);
            score = -raw;
            first_move = false;
        } else {
            // 後續步：零視窗試探 (沒有 LMR，確保每一步都嚴謹驗證)
            int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -alpha - 1, -alpha);
            score = -raw;

            // PVS 驗證失敗：重新用全視窗搜尋
            if (score > alpha && score < beta) {
                raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -beta, -score);
                score = -raw;
            }
        }

        if(score > best_score) best_score = score;
        if(best_score > alpha) alpha = best_score;
        
        // Beta 剪枝與歷史/殺手學習
        if(alpha >= beta) { 
            int opp = 1 - state->player;
            bool is_capture = (state->board.board[opp][action.second.first][action.second.second] != 0);
            
            if (!is_capture) {
                if (ply < 128 && killer_moves[ply][0] != action) {
                    killer_moves[ply][1] = killer_moves[ply][0];
                    killer_moves[ply][0] = action;
                }
                history_table[state->player][action.first.first][action.first.second][action.second.first][action.second.second] += depth * depth;
            }
            break; 
        }
        if(ctx.stop) break;
    }

    history.pop(hash_key);

    if (!ctx.stop) {
        TTFlag flag = TT_EXACT;
        if (best_score <= original_alpha) flag = TT_UPPERBOUND;
        else if (best_score >= beta) flag = TT_LOWERBOUND;
        tt_entry = {hash_key, depth, best_score, flag};
    }

    return best_score;
}

/*============================================================
 * MiniMax — search (終極時間管理：保底與救援機制)
 *============================================================*/
SearchResult MiniMax::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult best_result_overall;
    best_result_overall.depth = 0;
    best_result_overall.score = -M_MAX; // 預設為極低分

    // 清空歷史記憶體
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_table, 0, sizeof(history_table));

    if(state->legal_actions.empty()){
        state->get_legal_actions();
    }

    // =========================================================
    // 🛡️ 防線一：絕對保底機制 (Absolute Fallback)
    // 萬一系統連 0.001 秒都不給，剛進迴圈就超時，
    // 我們無條件把「第一步合法步」當作備案，絕對不交白卷！
    // =========================================================
    if (!state->legal_actions.empty()) {
        best_result_overall.best_move = state->legal_actions[0];
    }

    if(state->game_state == WIN){
        best_result_overall.score = P_MAX - state->step;
        best_result_overall.nodes = ctx.nodes;
        best_result_overall.seldepth = ctx.seldepth;
        best_result_overall.pv.clear();
        return best_result_overall;
    }
    if(state->game_state == DRAW){
        best_result_overall.score = 0;
        best_result_overall.nodes = ctx.nodes;
        best_result_overall.seldepth = ctx.seldepth;
        best_result_overall.pv.clear();
        return best_result_overall;
    }

    int total_moves = (int)state->legal_actions.size();
    Move prev_best_move; 
    bool has_prev_best_move = false;

    // 🌟 迭代加深 (Iterative Deepening)
    for (int current_depth = 1; current_depth <= depth; current_depth++) {
        
        SearchResult current_result;
        current_result.depth = current_depth;
        current_result.best_move = best_result_overall.best_move; // 繼承上一層的保底
        
        int best_score = M_MAX; 
        int alpha = M_MAX;
        int beta = P_MAX;
        int move_index = 0;

        // 根節點排序：PV (上一層最佳解) 絕對優先
        int num_moves = state->legal_actions.size();
        int move_scores[256];
        for (int i = 0; i < num_moves; ++i) {
            move_scores[i] = score_action(state, state->legal_actions[i], 0);
            if (has_prev_best_move && state->legal_actions[i] == prev_best_move) {
                move_scores[i] += 1000000; 
            }
        }

        // 🌟 即時挑選 (Selection Sort)
        for(int i = 0; i < num_moves; ++i) {
            int best_idx = i;
            for (int j = i + 1; j < num_moves; ++j) {
                if (move_scores[j] > move_scores[best_idx]) best_idx = j;
            }
            std::swap(state->legal_actions[i], state->legal_actions[best_idx]);
            std::swap(move_scores[i], move_scores[best_idx]);

            auto& action = state->legal_actions[i];
            State* next = &state_stack[0];
            state->apply_move(action, *next); 

            int score;
            if (i == 0) {
                int raw = eval_ctx(next, current_depth - 1, history, 1, ctx, p, -beta, -alpha);
                score = -raw;
            } else {
                int raw = eval_ctx(next, current_depth - 1, history, 1, ctx, p, -alpha - 1, -alpha);
                score = -raw;

                if (score > alpha && score < beta) {
                    raw = eval_ctx(next, current_depth - 1, history, 1, ctx, p, -beta, -score);
                    score = -raw;
                }
            }
            
            // 🚨 時間到了，立刻中斷此步的後續計算
            if (ctx.stop) break; 

            if(move_index == 0 || score > best_score){
                best_score = score;
                current_result.best_move = action;
                current_result.score = best_score;
                current_result.pv = {action};

                if (best_score > alpha) alpha = best_score;

                if(p.report_partial && ctx.on_root_update){
                   ctx.on_root_update({current_result.best_move, best_score, current_depth, move_index + 1, total_moves});
                }
            }
            move_index++;
        }

        // =========================================================
        // 🛡️ 防線二：部分結果救援 (Partial Rescue)
        // =========================================================
        if (ctx.stop) {
            // 如果這層被強制中斷，但我們已經算完了第一步 (move_index > 0)，
            // 且這步的分數甚至比上一層的最佳分數還要高，我們就大膽採用這個熱騰騰的半成品！
            // 否則，我們就直接 break，保留上一層完整算完的 best_result_overall。
            if (move_index > 0 && current_result.score > best_result_overall.score) {
                best_result_overall = current_result;
                best_result_overall.nodes = ctx.nodes;
                best_result_overall.seldepth = ctx.seldepth;
            }
            break; 
        }

        // 這層完整算完了，更新全域最佳解
        best_result_overall = current_result;
        best_result_overall.nodes = ctx.nodes;
        best_result_overall.seldepth = ctx.seldepth;
        
        prev_best_move = current_result.best_move;
        has_prev_best_move = true;

        // 提早結束機制：發現將死直接下棋
        if (best_score > 900000) break; 
    }

    return best_result_overall;
}

ParamMap MiniMax::default_params(){ return {{"UseKPEval", "true"}, {"UseEvalMobility", "true"}, {"ReportPartial", "true"}}; }
std::vector<ParamDef> MiniMax::param_defs(){ return {{"UseKPEval", ParamDef::CHECK, "true"}, {"UseEvalMobility", ParamDef::CHECK, "true"}, {"ReportPartial", ParamDef::CHECK, "true"}}; }
