#include <utility>
#include "state.hpp"
#include "minimax.hpp"


/*============================================================
 * MiniMax — eval_ctx
 *
 * Negamax with Alpha-Beta pruning. Caller manages memory.
 *============================================================*/
// 🌟 提醒：記得去 minimax.hpp 把 eval_ctx 的宣告加上 alpha 和 beta！
// int eval_ctx(..., int alpha = M_MAX, int beta = P_MAX);
int MiniMax::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p,
    int alpha, // 🌟 新增：目前能確保的最低保底分數 (預設極小)
    int beta   // 🌟 新增：對手能容忍的最高上限分數 (預設極大)
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    /* === Lazy move generation (sets game_state) === */
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    /* === Terminal / leaf checks === */
    if(state->game_state == DRAW){
        return 0;
    }
    if(state->game_state == WIN){
        return P_MAX - ply;
    }

    /* === Repetition check (game-specific) === */
    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    if(depth <= 0){
        int score = state->evaluate(
            p.use_kp_eval, p.use_eval_mobility, &history
        ); 
        history.pop(state->hash());
        return score;
    }

    /* === Negamax loop === */
    int best_score = M_MAX; // 保持極小值

    for(auto& action : state->legal_actions){

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        // 🌟 Alpha-Beta 視角反轉：
        // 如果換對手下 (!same)，則對手的保底(alpha)是我們的上限(-beta)，對手的上限(beta)是我們的保底(-alpha)
        // 如果還是自己下 (same)，則視角不變，alpha 與 beta 照舊傳遞
        int next_alpha = same ? alpha : -beta;
        int next_beta  = same ? beta  : -alpha;

        int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p, next_alpha, next_beta);
        int child_score = same ? raw : -raw;

        delete next;

        if(child_score > best_score){
            best_score = child_score;
        }

        // 🌟 Alpha-Beta 剪枝核心邏輯
        if (best_score > alpha) {
            alpha = best_score; // 更新我們的保底分數
        }
        if (alpha >= beta) {
            break; // ✂️ 剪枝！對手絕對不會讓我們走到這個局面，剩下的走法不用看了
        }

        if(ctx.stop){
            break;
        }
    }

    history.pop(state->hash());
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
        result.score = P_MAX;
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

    // 🌟 修復 1：移除 -10 防止溢位
    int best_score = M_MAX; 
    
    // 🌟 初始化根節點的 alpha 和 beta
    int alpha = M_MAX;
    int beta = P_MAX;
    
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    for(auto& action : state->legal_actions){

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        
        int next_alpha = same ? alpha : -beta;
        int next_beta  = same ? beta  : -alpha;

        int raw = eval_ctx(next, depth - 1, history, 1, ctx, p, next_alpha, next_beta);
        int score = same ? raw : -raw;
        
        delete next;

        // 🌟 修復 2：加入 move_index == 0 作為絕對保底，確保 AI 絕不回傳空指標
        if(move_index == 0 || score > best_score){
            best_score = score;
            result.best_move = action;
            result.score = best_score;
            result.pv = {action};

            // 🌟 在根節點也要持續更新 alpha 門檻
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
