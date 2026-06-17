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
        int current_player = state->player;
        int opp = 1 - current_player;
        int8_t attacker = state->board.board[current_player][action.first.first][action.first.second];
        int8_t victim = state->board.board[opp][action.second.first][action.second.second];
        
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

        if (ply >= state_stack.size()) state_stack.resize(ply + 2);

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

    if (ply >= state_stack.size()) state_stack.resize(ply + 2);

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
 * MiniMax — search
 *============================================================*/
SearchResult MiniMax::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        result.score = P_MAX - state->step;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        result.pv.clear();
        return result;
    }
    if(state->game_state == DRAW){
        result.score = 0;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        result.pv.clear();
        return result;
    }

    int best_score = M_MAX; 
    int alpha = M_MAX;
    int beta = P_MAX;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&state](const auto& a, const auto& b) {
        return score_action(state, a) > score_action(state, b);
    });

    bool first_move = true;

    for(auto& action : state->legal_actions){

        State* next = &state_stack[0];
        state->apply_move(action, *next); 

        int score;

        if (first_move) {
            int raw = eval_ctx(next, depth - 1, history, 1, ctx, p, -beta, -alpha);
            score = -raw;
            first_move = false;
        } else {
            int raw = eval_ctx(next, depth - 1, history, 1, ctx, p, -alpha - 1, -alpha);
            score = -raw;

            if (score > alpha && score < beta) {
                raw = eval_ctx(next, depth - 1, history, 1, ctx, p, -beta, -score);
                score = -raw;
            }
        }
        
        if(move_index == 0 || score > best_score){
            best_score = score;
            result.best_move = action;
            result.score = best_score;
            result.pv = {action};

            if (best_score > alpha) {
                alpha = best_score;
            }

            if(p.report_partial && ctx.on_root_update){
               ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }
        move_index++;
    }

    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    result.depth = depth;
    return result;
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