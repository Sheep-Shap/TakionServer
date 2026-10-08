#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fecrs {
// data: k юнитов подряд по unit_size байт. Возвращает m юнитов чётности подряд.
std::vector<uint8_t> encode(const uint8_t* data, size_t unit_size, unsigned k, unsigned m);
// Сколько парити-пакетов добавить к кадру из k пакетов (~12%); 0 если кадр слишком большой.
unsigned units_for(size_t k);
}