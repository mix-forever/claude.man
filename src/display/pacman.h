#pragma once

// Arcade-style Pac-Man corridor (y=12..57): double maze walls, square pellets,
// Pac-Man moving right as the 5h quota is consumed, Blinky waiting at the
// alarm threshold and chasing once it is crossed, "GAME OVER" at 100 %.
//
// usedFraction: 0.0 = nothing used (all pellets ahead of Pac-Man)
//               1.0 = quota exhausted (every pellet eaten)

void pacmanSetFraction(float usedFraction);
void pacmanSetThreshold(float thr);   // alarm threshold 0..1, 0 = no ghost
void pacmanDraw();                    // full corridor redraw
void pacmanTick();                    // mouth / ghost animation — call from loop()
