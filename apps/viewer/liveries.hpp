#pragma once
#include <string>
#include <vector>

#include "raylib.h"

// The grid's paint schemes: one entry per car slot (team livery with that car's
// number), read from assets/cars/f1_gearari/teams.json, and which slot each car in
// the race wears. teamColor() and the car model both go through this.
struct CarLivery {
    std::string team;   // e.g. "Papaya Masterkard F1"
    std::string key;    // base livery name, e.g. "papaya"
    std::string file;   // full path of the numbered livery PNG
    int number = 0;
    Color color{200, 200, 200, 255};  // for the HUD: the team's most recognisable colour
};

// Reads teams.json, or without it one slot per liveries/*.png. Empty if neither exists.
std::vector<CarLivery> loadLiveries(const std::string& assetsDir);

void setLiveryTable(const std::vector<CarLivery>& table);
const std::vector<CarLivery>& liveryTable();
// Which slot each car (by race index) wears; cars beyond the list wear slot index % count.
void setCarLiveries(const std::vector<int>& slotOfCar);
int carLivery(int carIndex);
