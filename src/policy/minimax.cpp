#include <utility>
#include "state.hpp"
#include "minimax.hpp"


/*============================================================
 * MiniMax — eval_ctx
 *
 * Negamax without pruning. Caller manages memory.
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
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

    // [ Hackathon TODO 3-1 ]
    // return the score for a winning terminal state
    // Hint: prefer faster wins by using ply.
    
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
    int best_score = M_MAX;

    for(auto& action : state->legal_actions){

        // create the child state after applying action
        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        // search the child one level deeper
        int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p);

        // convert raw to the current player's perspective (handle same-player turns)
        int child_score = same ? raw : -raw;

        delete next;

        // update best_score if this child is better
        if(child_score > best_score){
            best_score = child_score;
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
 *
 * Iterate legal moves, call eval_ctx, return SearchResult.
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

    // If the current position is terminal at root, return its score immediately.
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

    int best_score = M_MAX - 10;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    for(auto& action : state->legal_actions){
        /* [ Hackathon TODO 4-1 ]
         * search this move like TODO 3, but starting from the root */

        // create child state for this root move
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw = eval_ctx(next, depth - 1, history, 1, ctx, p);
        int score = same ? raw : -raw;
        delete next;

        if(score > best_score){
            // keep this move if it is the best so far
            best_score = score;
            result.best_move = action;
            result.score = best_score;
            result.pv = {action};

            if(p.report_partial && ctx.on_root_update){
               ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }
        move_index++;
    }

    // update result fields and return
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
