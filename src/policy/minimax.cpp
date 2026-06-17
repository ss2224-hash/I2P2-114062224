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

    thread_local std::vector<State> state_stack(128);

    // 🌟 新增：殺手步記憶體 (記錄每一層最近兩步引發剪枝的非吃子步)
    thread_local Move killer_moves[128][2];

    // 🌟 新增：歷史步記憶體 (記錄整棵搜尋樹中，每種移動成功剪枝的次數)
    // 陣列維度：[玩家 0/1][起點 Row][起點 Col][終點 Row][終點 Col]
    thread_local int history_table[2][6][5][6][5];

    // 🌟 終極步法排序函數 (加入 Ply 參數以讀取殺手步)
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

        // 2. 次高優先：安靜步中的「殺手步 (Killer Moves)」
        if (ply < 128) {
            if (action == killer_moves[ply][0]) return 9000;
            if (action == killer_moves[ply][1]) return 8000;
        }

        // 3. 一般優先：安靜步的「歷史宏觀價值 (History Heuristic)」
        // 歷史分數通常會隨著搜尋累積，加上這層排序能讓 AI 自動學會佈局
        return history_table[current_player][from_r][from_c][to_r][to_c]; 
    }

    // 靜止搜尋 (不變)
    int quiescence_search(State *state, GameHistory& history, SearchContext& ctx, const MMParams& p, int alpha, int beta, int ply) {
        ctx.nodes++;
        if (ctx.stop) return 0;

        int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        if (stand_pat >= beta) return beta;
        if (alpha < stand_pat) alpha = stand_pat;

        if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
            state->get_legal_actions();
        }

        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const auto& a, const auto& b) {
            return score_action(state, a, ply) > score_action(state, b, ply);
        });

        if ((size_t)ply >= state_stack.size()) state_stack.resize(ply + 2);

        for (auto& action : state->legal_actions) {
            // 🌟 嚴格防禦：QS 僅搜尋吃子步，且 QS 不需要殺手步
            int current_player = state->player;
            int opp = 1 - current_player;
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
 * MiniMax — eval_ctx
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

    int rep_score;
    if(state->check_repetition(history, rep_score)) return rep_score;
    history.push(hash_key);

    if(depth <= 0){
        int qs_score = quiescence_search(state, history, ctx, p, alpha, beta, ply);
        history.pop(hash_key);
        return qs_score;
    }

    // 🌟 排序時傳入 ply，讀取殺手與歷史分數
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
            int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -beta, -alpha);
            score = -raw;
            first_move = false;
        } else {
            int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -alpha - 1, -alpha);
            score = -raw;

            if (score > alpha && score < beta) {
                raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -beta, -score);
                score = -raw;
            }
        }

        if(score > best_score) best_score = score;
        if(best_score > alpha) alpha = best_score;
        
        // 🌟 剪枝觸發！學習時間：更新殺手與歷史記憶體
        if(alpha >= beta) { 
            int opp = 1 - state->player;
            int to_r = action.second.first;
            int to_c = action.second.second;
            
            // 只有「非吃子步」才有資格成為殺手與歷史（吃子步已經有 MVV-LVA 照顧了）
            if (state->board.board[opp][to_r][to_c] == 0) {
                // 1. 儲存殺手步
                if (ply < 128 && killer_moves[ply][0] != action) {
                    killer_moves[ply][1] = killer_moves[ply][0];
                    killer_moves[ply][0] = action;
                }
                // 2. 增加歷史步的分數 (深度越深，代表這招越強，加分平方級提升)
                history_table[state->player][action.first.first][action.first.second][to_r][to_c] += depth * depth;
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
 * MiniMax — search (迭代加深)
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

    // 🌟 每回合重新思考前，將舊的歷史與殺手記憶體衰減或清空，避免無限膨脹
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_table, 0, sizeof(history_table));

    if(!state->legal_actions.size()){
        state->get_legal_actions();
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

    for (int current_depth = 1; current_depth <= depth; current_depth++) {
        
        SearchResult current_result;
        current_result.depth = current_depth;
        int best_score = M_MAX; 
        int alpha = M_MAX;
        int beta = P_MAX;
        int move_index = 0;

        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const auto& a, const auto& b) {
            int score_a = score_action(state, a, 0); // Root 是 0 層
            int score_b = score_action(state, b, 0);
            
            if (has_prev_best_move) {
                if (a == prev_best_move) score_a += 1000000;
                if (b == prev_best_move) score_b += 1000000;
            }
            return score_a > score_b;
        });

        bool first_move = true;

        for(auto& action : state->legal_actions){
            State* next = &state_stack[0];
            state->apply_move(action, *next); 

            int score;

            if (first_move) {
                int raw = eval_ctx(next, current_depth - 1, history, 1, ctx, p, -beta, -alpha);
                score = -raw;
                first_move = false;
            } else {
                int raw = eval_ctx(next, current_depth - 1, history, 1, ctx, p, -alpha - 1, -alpha);
                score = -raw;

                if (score > alpha && score < beta) {
                    raw = eval_ctx(next, current_depth - 1, history, 1, ctx, p, -beta, -score);
                    score = -raw;
                }
            }
            
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

        if (ctx.stop) break; 

        best_result_overall = current_result;
        best_result_overall.nodes = ctx.nodes;
        best_result_overall.seldepth = ctx.seldepth;
        
        prev_best_move = current_result.best_move;
        has_prev_best_move = true;

        if (best_score > 900000) break; 
    }

    return best_result_overall;
}

ParamMap MiniMax::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> MiniMax::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}