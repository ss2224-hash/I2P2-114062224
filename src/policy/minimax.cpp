#include <utility>
#include <algorithm>
#include <vector>
#include "state.hpp"
#include "minimax.hpp"

// ============================================================
// 🌟 效能大躍進 1：扁平化陣列置換表 (Flat Array TT)
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

    // 🌟 效能大躍進 2：預先分配好的「狀態池 (State Pool)」
    thread_local std::vector<State> state_stack(128);

    // 🌟 輔助函數：MVV-LVA (獨立變數 current_player，避開 p 衝突)
    int score_action(State* state, const Move& action) {
        int from_r = action.first.first;
        int from_c = action.first.second;
        int to_r   = action.second.first;
        int to_c   = action.second.second;
        
        int current_player = state->player;
        int opp = 1 - current_player;
        int8_t attacker = state->board.board[current_player][from_r][from_c];
        int8_t victim   = state->board.board[opp][to_r][to_c];
        
        if (victim != 0) {
            static const int val[7] = {0, 10, 30, 30, 50, 90, 1000};
            return 10000 + val[victim] * 10 - val[attacker]; // 吃子步優先
        }
        return 0; // 一般移動
    }

    // 🌟 靜止搜尋：加入 ply 參數，參數順序對齊
    int quiescence_search(State *state, GameHistory& history, SearchContext& ctx, const MMParams& p, int alpha, int beta, int ply) {
        ctx.nodes++;
        if (ctx.stop) return 0;

        int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        
        if (stand_pat >= beta) return beta;
        if (alpha < stand_pat) alpha = stand_pat;

        if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
            state->get_legal_actions();
        }

        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&state](const auto& a, const auto& b) {
            return score_action(state, a) > score_action(state, b);
        });

        // 🌟 加上 (size_t) 強制轉型
        if ((size_t)ply >= state_stack.size()) state_stack.resize(ply + 2);

        for (auto& action : state->legal_actions) {
            if (score_action(state, action) == 0) continue; // 🌟 嚴格防禦：QS 僅搜尋吃子步

            State* next = &state_stack[ply];
            state->apply_move(action, *next);

            // 遞迴時帶上 ply + 1，並直接反轉 -beta, -alpha
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
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

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
    if(state->game_state == WIN) return P_MAX - state->step; // 鼓勵快贏

    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(hash_key);

    if(depth <= 0){
        // 正確傳入 7 個參數，包含 ply
        int qs_score = quiescence_search(state, history, ctx, p, alpha, beta, ply);
        history.pop(hash_key);
        return qs_score;
    }

    std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&state](const auto& a, const auto& b) {
        return score_action(state, a) > score_action(state, b);
    });

    // 🌟 加上 (size_t) 強制轉型
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
            // Null Window Search (PVS)
            int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -alpha - 1, -alpha);
            score = -raw;

            if (score > alpha && score < beta) {
                raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, -beta, -score);
                score = -raw;
            }
        }

        if(score > best_score) best_score = score;
        if(best_score > alpha) alpha = best_score;
        if(alpha >= beta) break; 
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
 * MiniMax — search (🚀 升級：迭代加深 Iterative Deepening)
 *============================================================*/
SearchResult MiniMax::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult best_result_overall; // 用來儲存「上一層完整算完」的最佳結果
    best_result_overall.depth = 0;

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
    
    // 🌟 迭代加深專屬記憶：記錄上一層深度的最佳走法
    Move prev_best_move; 
    bool has_prev_best_move = false;

    // =========================================================
    // 🌟 迭代加深迴圈 (從深度 1 一路算到目標 depth)
    // =========================================================
    for (int current_depth = 1; current_depth <= depth; current_depth++) {
        
        SearchResult current_result;
        current_result.depth = current_depth;
        int best_score = M_MAX; 
        int alpha = M_MAX;
        int beta = P_MAX;
        int move_index = 0;

        // 🌟 終極排序魔法：MVV-LVA + 上一層最佳步優先 (PV Move)
        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const auto& a, const auto& b) {
            int score_a = score_action(state, a);
            int score_b = score_action(state, b);
            
            // 如果我們有上一層的最佳步，給予它絕對壓倒性的超高分 (100萬分)
            // 確保它絕對是第一個被 PVS 以全視窗搜尋的動作！
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
            
            // 🚨 時間管理保護機制：如果在思考這步棋的時候超時了，立刻放棄！
            if (ctx.stop) {
                break; 
            }

            if(move_index == 0 || score > best_score){
                best_score = score;
                current_result.best_move = action;
                current_result.score = best_score;
                current_result.pv = {action};

                if (best_score > alpha) {
                    alpha = best_score;
                }

                if(p.report_partial && ctx.on_root_update){
                   ctx.on_root_update({current_result.best_move, best_score, current_depth, move_index + 1, total_moves});
                }
            }
            move_index++;
        }

        // 🚨 迭代中斷處理：如果這次深度沒算完就被強制停止，
        // 我們「不要」把這次殘缺的結果更新回去，而是直接跳出迴圈，
        // 這樣函數最後就會回傳上一層完整算完的 `best_result_overall`。
        if (ctx.stop) {
            break; 
        }

        // 這次深度完整算完了！更新大盤紀錄
        best_result_overall = current_result;
        best_result_overall.nodes = ctx.nodes;
        best_result_overall.seldepth = ctx.seldepth;
        
        // 把這次找出來的絕佳好步記下來，留給下一層深度當作排序依據
        prev_best_move = current_result.best_move;
        has_prev_best_move = true;

        // 💡 提早結束機制 (Early Exit)：
        // 如果我們已經找到保證將死對手的方法 (分數極高，接近 P_MAX)，
        // 就不需要再浪費時間往下算了，直接拿著將死的步法去下棋吧！
        if (best_score > 900000) { 
            break; 
        }
    }

    return best_result_overall;
}

/*============================================================
 * MiniMax — default_params / param_defs
 *============================================================*/
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