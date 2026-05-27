#pragma once

// Rysuje wiersz Pacmana (y=12..57) na podstawie ułamka zużycia.
// usedFraction: 0.0 = nic nie zużyte (wszystkie kropki przed Pacmanem)
//               1.0 = wszystko zużyte (Pacman zjadł wszystko)

void pacmanSetFraction(float usedFraction);
void pacmanDraw();        // pełny przerys wiersza
void pacmanTick();        // animacja pyszczka — wywoływać w loop()
