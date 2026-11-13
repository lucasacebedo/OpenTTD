id	unit	kind	frequency	source	version	equation
gdp_production	real_pounds	flow	month	transport value added minus route subsidies	economy-lab/1.0	E-006
gdp_expenditure	real_pounds	flow	month	consumption, investment, government and net exports	economy-lab/1.0	E-007
statistical_discrepancy	real_pounds	flow	month	production minus expenditure	economy-lab/1.0	E-008
logistics_volume	cargo_pieces	flow	month	accepted deliveries kept by cargo type	economy-lab/1.0	none
potential_output	real_pounds	stock	month	calibrated level, population, capital and capacity	economy-lab/1.0	E-027
output_gap	rate_bp	stock	month	goods demand relative to potential	economy-lab/1.0	bridge
consumption	real_pounds	flow	month	passenger and mail tariffs	economy-lab/1.0	E-007
investment	real_pounds	flow	month	construction and new vehicles	economy-lab/1.0	E-007
government_purchases	real_pounds	flow	month	1000 policy points equal calibrated GDP	economy-lab/1.0	E-007
tax_revenue	real_pounds	flow	month	tax rate and autonomous tax	economy-lab/1.0	E-030
subsidies	real_pounds	flow	month	route subsidy, a transfer	economy-lab/1.0	E-006
exports	real_pounds	flow	month	real exchange rate	economy-lab/1.0	E-044
imports	real_pounds	flow	month	activity and the real exchange rate	economy-lab/1.0	E-045
policy_rate	rate_bp	stock	month	Taylor rule	economy-lab/1.0	E-032
loan_rate	rate_bp	stock	month	policy rate plus risk premium	economy-lab/1.0	E-038
world_rate	rate_bp	stock	month	exogenous world rate	economy-lab/1.0	E-051
risk_premium	rate_bp	stock	month	public debt over GDP	economy-lab/1.0	E-049
inflation	rate_bp	flow	month	expectations-augmented Phillips curve	economy-lab/1.0	Phillips
expected_inflation	rate_bp	stock	month	adaptive expectations	economy-lab/1.0	bridge
unemployment	rate_bp	stock	month	Okun bridge	economy-lab/1.0	E-022
population	persons	stock	month	town populations	economy-lab/1.0	none
labour_force	persons	stock	month	45 percent of population	economy-lab/1.0	E-020
employment	persons	stock	month	labour force times one minus unemployment	economy-lab/1.0	E-022
exchange	permyriad	stock	month	spot rate	economy-lab/1.0	E-053
fundamental_exchange	permyriad	stock	month	purchasing-power parity	economy-lab/1.0	E-052
overshooting	indicator	stock	month	spot above the fundamental	economy-lab/1.0	E-053
public_debt	real_pounds	stock	month	treasury bonds	economy-lab/1.0	E-031
debt_service	real_pounds	flow	month	contracted coupon	economy-lab/1.0	E-031
bank_loans	real_pounds	stock	month	company loans	economy-lab/1.0	E-035
bank_deposits	real_pounds	stock	month	household saving	economy-lab/1.0	E-034
bank_reserves	real_pounds	stock	month	vault cash and reserve balances	economy-lab/1.0	E-033
bank_bonds	real_pounds	stock	month	treasury bonds held by banks	economy-lab/1.0	E-035
bank_equity	real_pounds	stock	month	opening equity plus balanced flows	economy-lab/1.0	E-035
bank_gap	real_pounds	stock	month	bank assets minus liabilities	economy-lab/1.0	E-035
central_bank_bonds	real_pounds	stock	month	open-market holdings	economy-lab/1.0	E-033
base_money	real_pounds	stock	month	currency plus reserve balances	economy-lab/1.0	E-033
official_reserves	real_pounds	stock	month	external reserve asset	economy-lab/1.0	BOP
household_bonds	real_pounds	stock	month	treasury bonds held outside banks and the central bank	economy-lab/1.0	E-031
nfa	real_pounds	stock	month	net foreign assets	economy-lab/1.0	BOP
price_index	index	stock	month	Phillips price level, 1000 is the base	economy-lab/1.0	Phillips
money_supply	policy_points	stock	month	exogenous nominal money setting	economy-lab/1.0	none
cargo_capacity	slots	stock	month	installed vehicle slots	economy-lab/1.0	none
capital	real_pounds	stock	month	gross investment minus depreciation	economy-lab/1.0	E-018
calibration_error	indicator	stock	month	1 when GDP leaves the E-027 range	economy-lab/1.0	A-09
clamp_count	count	stock	month	series sitting on a safety clamp	economy-lab/1.0	A-21
run_quality	permyriad	stock	month	10000 when the identity, calibration and clamps are clean	economy-lab/1.0	AC-16
expected_loss	real_pounds	stock	month	exposure times PD times LGD	economy-lab/1.0	PD*LGD*EAD
