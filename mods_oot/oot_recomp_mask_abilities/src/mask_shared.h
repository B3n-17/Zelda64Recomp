#ifndef MASK_SHARED_H
#define MASK_SHARED_H

#include "player.h"

/*
 * Whether Link is in the water.
 *
 * This is the flag the player actor itself uses to decide the water is worth
 * slowing everything down for - z_player.c reads it to set sWaterSpeedFactor to
 * 0.5f - so it is also the right line to draw between an ability that works on
 * land and one that works in water.
 */
#define PLAYER_IS_IN_WATER(this) (((this)->stateFlags1 & PLAYER_STATE1_27) != 0)

#endif
