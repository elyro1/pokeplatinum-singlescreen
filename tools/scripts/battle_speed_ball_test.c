#include <assert.h>
#include <stdio.h>

#include "../../src/battle_anim/ov12_02235E94.c"

static void CheckMilestone(int command, int start, int complete, int following, BOOL lateFirstPoll)
{
    BallRotation ball = { .unk_90.mode = BALL_THROW_MODE_THROW };
    for (int repeat = 0; repeat < 3; repeat++) {
        ov12_022368C8(&ball, command);
        if (lateFirstPoll) {
            // The ball can automatically enter the next phase while the
            // controller is still waiting before its first completion poll.
            ov12_022368E4(&ball, start);
        } else {
            assert(ov12_022368D0(&ball, command));
        }
        assert(ball.unk_14 == start);

        // The animation reaches its milestone in the first Turbo step, then
        // moves past it before the unscaled capture controller gets to poll.
        ov12_022368E4(&ball, complete);
        ov12_022368E4(&ball, following);
        assert(!ov12_022368D0(&ball, command));
        assert(!ov12_022368D0(&ball, command));
        assert(ball.unk_14 == following); // Never replay a completed ball phase.
    }
}

int main(void)
{
    const int stages[][4] = {
        { 0, 1, 2, 3 },   // Throw: caught-message wait can otherwise miss state 2.
        { 1, 3, 4, 5 },   // Opening/absorption.
        { 2, 5, 6, 7 },   // Trainer-blocked ball.
        { 3, 7, 14, 15 }, // Landing.
        { 4, 15, 18, 15 },// Repeated shakes must consume the previous completion.
        { 5, 19, 27, 0 },
        { 6, 21, 22, 23 },
        { 7, 23, 27, 0 }, // Caught-ball cleanup.
    };
    for (int stage = 0; stage < NELEMS(stages); stage++) {
        for (int late = 0; late < 2; late++) {
            CheckMilestone(stages[stage][0], stages[stage][1], stages[stage][2], stages[stage][3], late);
        }
    }
    BallRotation sendOut = { .unk_90.mode = BALL_THROW_MODE_SEND_OUT };
    ov12_022368E4(&sendOut, 2);
    ov12_022368E4(&sendOut, 3);
    assert(!ov12_0223782C(&sendOut, 2));
    assert(ov12_0223782C(&sendOut, 3));
    puts("Capture ball: all eight commands retain Turbo completion milestones; repeated shakes reset them; send-outs preserve exact-state checks.");
}
