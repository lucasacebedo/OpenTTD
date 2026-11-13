/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file units.hpp Strong units for the economy lab. Different units do not add. */

#ifndef ECONOMYLAB_UNITS_HPP
#define ECONOMYLAB_UNITS_HPP

#include <cstdint>
#include <type_traits>

/** Dimension of a stored integer. Conversion between dimensions is a named function, never a cast. */
enum class Unit : uint8_t {
	CargoPieces, ///< Physical units of one cargo type. Not a measure of GDP.
	NominalPounds, ///< Current pounds, as paid by the game.
	RealPounds, ///< Pounds at the 1920 payment-index base.
	PolicyPoints, ///< Slider units. Not pounds and not cargo.
	RateBp, ///< Hundredths of a percentage point. 100 is 1.00 percent.
	Permyriad, ///< Ten-thousandths. 10000 is one.
};

/**
 * Integer quantity that only combines with the same unit.
 * @tparam U The dimension stored in @a value.
 */
template <Unit U>
struct Quantity {
	int64_t value = 0;

	constexpr Quantity() = default;
	explicit constexpr Quantity(int64_t raw) : value(raw) {}

	constexpr int64_t Raw() const { return this->value; }

	friend constexpr Quantity operator+(Quantity a, Quantity b) { return Quantity{a.value + b.value}; }
	friend constexpr Quantity operator-(Quantity a, Quantity b) { return Quantity{a.value - b.value}; }
	friend constexpr bool operator==(Quantity a, Quantity b) { return a.value == b.value; }
};

using CargoPieces = Quantity<Unit::CargoPieces>;
using NominalPounds = Quantity<Unit::NominalPounds>;
using RealPounds = Quantity<Unit::RealPounds>;
using PolicyPoints = Quantity<Unit::PolicyPoints>;
using RateBp = Quantity<Unit::RateBp>;

/** Conversion version recorded in docs/model/variables.yaml. */
static constexpr int ECONOMYLAB_CONVERSION_VERSION = 2;

/** 1000 policy points equal the calibrated real GDP passed by the caller. */
static constexpr int64_t POLICY_POINTS_PER_CALIBRATED_GDP = 1000;

/** Payment index that means "no inflation since 1920". */
static constexpr int64_t PAYMENT_INDEX_BASE = 1 << 16;

/**
 * Nominal pounds to 1920 pounds, using the game's payment index.
 * Integer division. A zero or tiny index is treated as the 1920 base.
 */
constexpr RealPounds DeflatePounds(NominalPounds nominal, int64_t payment_index)
{
	if (nominal.Raw() == 0) return RealPounds{0};
	if (payment_index < PAYMENT_INDEX_BASE) payment_index = PAYMENT_INDEX_BASE;
	const int64_t pounds = nominal.Raw();
	const int64_t whole = pounds / payment_index;
	const int64_t rest = pounds % payment_index;
	return RealPounds{whole * PAYMENT_INDEX_BASE + rest * PAYMENT_INDEX_BASE / payment_index};
}

constexpr RealPounds PolicyPointsToReal(PolicyPoints points, RealPounds calibrated_gdp)
{
	if (points.Raw() == 0 || calibrated_gdp.Raw() <= 0) return RealPounds{0};
	return RealPounds{points.Raw() * calibrated_gdp.Raw() / POLICY_POINTS_PER_CALIBRATED_GDP};
}

static_assert(!std::is_same_v<RealPounds, CargoPieces>);
static_assert(!std::is_same_v<RealPounds, NominalPounds>);
static_assert(!std::is_same_v<RealPounds, PolicyPoints>);
static_assert((RealPounds{3} + RealPounds{4}).Raw() == 7);
static_assert(DeflatePounds(NominalPounds{PAYMENT_INDEX_BASE}, PAYMENT_INDEX_BASE).Raw() == PAYMENT_INDEX_BASE);
static_assert(PolicyPointsToReal(PolicyPoints{1000}, RealPounds{5000}).Raw() == 5000);

#endif /* ECONOMYLAB_UNITS_HPP */
