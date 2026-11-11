/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CLK_CPU_OSM_NOTHING_H
#define _CLK_CPU_OSM_NOTHING_H

/* Encoding targets, not a claim about the firmware-owned CPU PLL's VCO. */
#define NOTHING_OC_PWRCL_LVAL		110
#define NOTHING_OC_PERFCL_LVAL		135
#define NOTHING_STOCK_PWRCL_LVAL		94
#define NOTHING_STOCK_PERFCL_LVAL		120
#define NOTHING_OC_MIN_MV_STEP		4
#define NOTHING_OC_MAX_MV_STEP		8
#define NOTHING_OC_MAX_MV		1300 /* Data sanity check, NOT a safe voltage. */
#define NOTHING_OC_POLL_MS		100
#define NOTHING_OC_STALE_MS		500

struct osm_lut_row {
	u32 freq;
	u32 volt;
	u32 override;
};

static inline unsigned long nothing_oc_lut_rate(u32 word)
{
	return (word & GENMASK(31, 30)) ?
		XO_RATE * (word & OSM_FREQ_LVAL_MASK) : OSM_INIT_RATE;
}

/* Bounded extrapolation of the last stock slope; no stock OPP is changed. */
static inline int nothing_oc_voltage_step(u32 stock_l, u32 previous_l,
					 u32 target_l, u32 stock_mv,
					 u32 previous_mv)
{
	u32 step;

	if (!previous_l || previous_l >= stock_l || target_l <= stock_l ||
	    target_l > OSM_FREQ_LVAL_MASK || previous_mv < 400 ||
	    stock_mv < previous_mv || stock_mv > NOTHING_OC_MAX_MV)
		return -EINVAL;

	step = DIV_ROUND_UP((stock_mv - previous_mv) * (target_l - stock_l),
			    stock_l - previous_l);
	step = clamp_val(step, NOTHING_OC_MIN_MV_STEP, NOTHING_OC_MAX_MV_STEP);
	if (stock_mv + step > NOTHING_OC_MAX_MV)
		return -EINVAL;
	return step;
}

static inline bool nothing_oc_temperature_blocked(bool was_blocked,
		bool sensors_ok, int cpu_mc, int skin_mc, u32 cpu_trip,
		u32 cpu_resume, u32 skin_trip, u32 skin_resume)
{
	if (!sensors_ok || cpu_mc < 0 || cpu_mc > 125000 ||
	    skin_mc < 0 || skin_mc > 70000)
		return true;
	if ((u32)cpu_mc >= cpu_trip || (u32)skin_mc >= skin_trip)
		return true;
	return was_blocked && ((u32)cpu_mc > cpu_resume ||
			       (u32)skin_mc > skin_resume);
}

#endif
