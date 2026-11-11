/* SPDX-License-Identifier: GPL-2.0 */
/* Kernel-independent mocks. The tested functions are extracted, not reimplemented. */
#ifndef __aarch64__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned long long u64;
typedef _Bool bool;
#define true 1
#define false 0
#ifndef NULL
#define NULL ((void *)0)
#endif
#define EINVAL 22
#define EIO 5
#define ENOMEM 12
#define ENODEV 19
#define GFP_KERNEL 0
#define GENMASK(h, l) ((u32)((~0U << (l)) & (~0U >> (31 - (h)))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define clamp_val(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define smp_load_acquire(p) (*(p))
#define smp_store_release(p, v) (*(p) = (v))
#define time_after(a, b) ((long)((b) - (a)) < 0)
#define msecs_to_jiffies(ms) (ms)
#define wmb() __asm__ volatile("" ::: "memory")
#define OSM_INIT_RATE 300000000UL
#define XO_RATE 19200000UL
#define OSM_TABLE_SIZE 40
#define OSM_REG_SIZE 32
#define FREQ_REG 0x110
#define VOLT_REG 0x114
#define OVERRIDE_REG 0x118
#define OSM_FREQ_LVAL_MASK GENMASK(7, 0)
#define OSM_FREQ_ROW_MASK GENMASK(15, 8)
#define CORE_COUNT_VAL(v) (((v) & GENMASK(18, 16)) >> 16)
#define SINGLE_CORE_COUNT 1
#define DCVS_PERF_STATE_DESIRED_REG(n) (0x920 + 4 * (n))
#define CPUFREQ_ENTRY_INVALID (~0U)
#define CPUFREQ_TABLE_END (~1U)
#define CPUFREQ_BOOST_FREQ 1
#define CPUFREQ_RELATION_L 0
#define CPUFREQ_RELATION_H 1

#include "clk-cpu-osm-nothing.h"

enum fault {
	ACCEPT, REJECT_ALL, REJECT_FREQ, REJECT_VOLT, REJECT_FIRST_FREQ,
	REJECT_SECOND_FREQ, BREAK_OVERRIDE, FAIL_ROLLBACK, REJECT_REQUEST,
	CHANGE_ON_REQUEST, DIVERGE_STOCK,
};

struct model {
	u32 regs[0x1400 / 4];
	u32 original[0x1400 / 4];
	u32 valid, stock_l, target_l, lut_writes, freq_writes, override_writes;
	u32 request_writes, last_request, stock_writes;
	enum fault fault;
};

struct clk_osm;
struct clk_hw { struct clk_osm *owner; struct clk_hw *parent; };
struct osm_entry { u16 virtual_corner, open_loop_volt; long frequency; };
struct clk_osm {
	struct clk_hw hw;
	struct osm_entry osm_table[OSM_TABLE_SIZE + 1];
	struct model *model;
	void *vbase;
	struct clk_osm *oc_parent;
	struct osm_lut_row oc_expected[2];
	u32 oc_freq_words[OSM_TABLE_SIZE], oc_volt_words[OSM_TABLE_SIZE];
	u32 oc_stock_entries, oc_stock_index, oc_stock_khz, oc_stock_limit_khz;
	u32 oc_target_khz, oc_row;
	u32 osm_table_size, core_num, cluster_num, related_cpus;
	unsigned long oc_thermal_sample;
	bool oc_active, oc_hw_verified, oc_thermal_blocked, oc_previous_allowed;
	bool oc_request_logged, per_core_dcvs;
};
struct cpufreq_frequency_table { u32 flags, driver_data, frequency; };
struct cpufreq_policy {
	struct clk_osm *driver_data;
	struct cpufreq_frequency_table *freq_table;
	struct { u32 min_freq, max_freq; } cpuinfo;
	u32 cpu, cur, min, max, cpus_storage, related_cpus;
	u32 *cpus;
	bool dvfs_possible_from_any_cpu, fast_switch_possible;
};
struct cpufreq_freqs { u32 old, new, flags; };

static struct model model;
static struct clk_osm cluster, core;
static struct cpufreq_policy policy;
static struct cpufreq_frequency_table table[OSM_TABLE_SIZE + 2];
static unsigned long jiffies;
static bool nothing_oc_suspended, is_sdmmagpie = true;
static u32 assertions, cases, log_count, arch_rate, notify_pre, notify_post;
static bool fail_allocation;

#ifdef __aarch64__
void *memset(void *p, int c, unsigned long size)
{
	unsigned char *s = p;
	while (size--)
		*s++ = c;
	return p;
}
void *memcpy(void *dst, const void *src, unsigned long size)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	while (size--)
		*d++ = *s++;
	return dst;
}
static void output(const char *text, unsigned long size)
{
	register long x0 __asm__("x0") = 1;
	register const char *x1 __asm__("x1") = text;
	register unsigned long x2 __asm__("x2") = size;
	register long x8 __asm__("x8") = 64;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
}
static __attribute__((noreturn)) void finish(int code)
{
	register long x0 __asm__("x0") = code;
	register long x8 __asm__("x8") = 93;
	__asm__ volatile("svc #0" : : "r"(x0), "r"(x8) : "memory");
	__builtin_unreachable();
}
#endif

static void check(bool ok, int line)
{
	assertions++;
	if (ok)
		return;
#ifndef __aarch64__
	fprintf(stderr, "OC selftest FAILED at line %d, case %u\n", line, cases);
	exit(1);
#else
	output("OC selftest FAILED\n", 19);
	finish(1);
#endif
}
#define CHECK(x) check(!!(x), __LINE__)

static void info_log(const char *fmt, ...)
{
	const char tag[] = "Nothing-OC:";
	u32 i;
	for (i = 0; i < sizeof(tag) - 1; i++)
		CHECK(fmt[i] == tag[i]);
	log_count++;
}
#define pr_info(...) info_log(__VA_ARGS__)
#define pr_err(...) do { } while (0)

static u32 clk_osm_read_reg_no_log(struct clk_osm *c, u32 offset)
{
	CHECK(offset < sizeof(c->model->regs) && !(offset & 3));
	return c->model->regs[offset / 4];
}
#define clk_osm_read_reg clk_osm_read_reg_no_log
static int clk_osm_mb(struct clk_osm *c) { return c->model->regs[0]; }

static void clk_osm_write_reg(struct clk_osm *c, u32 value, u32 offset)
{
	struct model *m = c->model;
	int kind = -1;
	u32 row = 0;
	if (offset >= 0x920 && offset < 0x920 + 6 * 4) {
		m->request_writes++;
		m->last_request = value;
		if (m->fault == REJECT_REQUEST && value >= m->valid)
			return;
		m->regs[offset / 4] = value;
		if (m->fault == CHANGE_ON_REQUEST && value >= m->valid)
			m->regs[(FREQ_REG + m->valid * OSM_REG_SIZE) / 4] =
				(m->regs[(FREQ_REG + m->valid * OSM_REG_SIZE) / 4] &
				 ~OSM_FREQ_LVAL_MASK) | m->stock_l;
		return;
	}
	if (offset >= FREQ_REG && (offset - FREQ_REG) % OSM_REG_SIZE == 0) {
		kind = 0; row = (offset - FREQ_REG) / OSM_REG_SIZE;
	} else if (offset >= VOLT_REG && (offset - VOLT_REG) % OSM_REG_SIZE == 0) {
		kind = 1; row = (offset - VOLT_REG) / OSM_REG_SIZE;
	} else if (offset >= OVERRIDE_REG && (offset - OVERRIDE_REG) % OSM_REG_SIZE == 0) {
		kind = 2; row = (offset - OVERRIDE_REG) / OSM_REG_SIZE;
	}
	CHECK(kind >= 0 && row < OSM_TABLE_SIZE);
	m->lut_writes++;
	if (row < m->valid)
		m->stock_writes++;
	CHECK(row == m->valid || row == m->valid + 1);
	if (kind == 2)
		m->override_writes++;
	if (kind == 0) {
		m->freq_writes++;
		if ((value & OSM_FREQ_LVAL_MASK) == m->target_l)
			CHECK(m->regs[(VOLT_REG + row * OSM_REG_SIZE) / 4] >
			      m->original[(VOLT_REG + row * OSM_REG_SIZE) / 4]);
	}
	if (m->fault == REJECT_ALL ||
	    (m->fault == REJECT_FREQ && kind == 0) ||
	    (m->fault == REJECT_VOLT && kind == 1) ||
	    (m->fault == REJECT_FIRST_FREQ && kind == 0 && row == m->valid + 1) ||
	    (m->fault == REJECT_SECOND_FREQ && kind == 0 && row == m->valid) ||
	    (m->fault == FAIL_ROLLBACK && kind == 0 &&
	     ((row == m->valid && (value & OSM_FREQ_LVAL_MASK) == m->target_l) ||
	      (row == m->valid + 1 && (value & OSM_FREQ_LVAL_MASK) == m->stock_l))))
		return;
	m->regs[offset / 4] = value;
	if (m->fault == BREAK_OVERRIDE && kind == 0 && row == m->valid + 1 &&
	    (value & OSM_FREQ_LVAL_MASK) == m->target_l)
		m->regs[(OVERRIDE_REG + row * OSM_REG_SIZE) / 4] ^= 1;
	if (m->fault == DIVERGE_STOCK && kind == 0 && row == m->valid + 1 &&
	    (value & OSM_FREQ_LVAL_MASK) == m->target_l)
		m->regs[(VOLT_REG + (m->valid - 1) * OSM_REG_SIZE) / 4]++;
}

static void osm_set_index(struct clk_osm *c, u32 index, u32 num)
{
	clk_osm_write_reg(c, index, DCVS_PERF_STATE_DESIRED_REG(num));
	clk_osm_mb(c);
}
static void arch_set_freq_scale(u32 cpus, u32 rate, u32 max) { arch_rate = rate; }
static struct clk_osm *osm_configure_policy(struct cpufreq_policy *p) { return &core; }
static struct clk_hw *clk_hw_get_parent(struct clk_hw *hw) { return hw->parent; }
static struct clk_osm *to_clk_osm(struct clk_hw *hw) { return hw->owner; }
static void cpumask_copy(u32 *dst, const u32 *src) { *dst = *src; }
static void *kcalloc(unsigned long n, unsigned long size, int flags)
{
	CHECK(n * size <= sizeof(table));
	if (fail_allocation)
		return NULL;
	memset(table, 0, sizeof(table));
	return table;
}
static void kfree(void *p) { }
static struct cpufreq_policy *cpufreq_cpu_get_raw(u32 cpu) { return &policy; }
static int cpufreq_table_validate_and_show(struct cpufreq_policy *p,
					 struct cpufreq_frequency_table *t)
{
	u32 i, min = ~0U, max = 0;
	for (i = 0; t[i].frequency != CPUFREQ_TABLE_END; i++) {
		CHECK(i < OSM_TABLE_SIZE + 1);
		if (t[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;
		if (t[i].frequency < min) min = t[i].frequency;
		if (t[i].frequency > max) max = t[i].frequency;
	}
	p->freq_table = t;
	p->cpuinfo.min_freq = p->min = min;
	p->cpuinfo.max_freq = p->max = max;
	return 0;
}
static int cpufreq_frequency_table_target(struct cpufreq_policy *p, u32 rate, u32 rel)
{
	u32 i, best = 0;
	for (i = 0; p->freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		u32 f = p->freq_table[i].frequency;
		if (f == CPUFREQ_ENTRY_INVALID) continue;
		if (rel == CPUFREQ_RELATION_L && f >= rate) return i;
		if (f <= rate) best = i;
	}
	return best;
}
static void cpufreq_freq_transition_begin(struct cpufreq_policy *p,
					  struct cpufreq_freqs *f) { notify_pre = f->new; }
static void cpufreq_freq_transition_end(struct cpufreq_policy *p,
					struct cpufreq_freqs *f, int ret)
{
	if (!ret) p->cur = notify_post = f->new;
}
static void cpufreq_verify_within_limits(struct cpufreq_policy *p, u32 min, u32 max)
{
	p->min = clamp_val(p->min, min, max);
	p->max = clamp_val(p->max, min, max);
	if (p->min > p->max) p->min = p->max;
}
static int cpufreq_generic_frequency_table_verify(struct cpufreq_policy *p) { return 0; }

#include "model-driver.h"

static void init(u32 number)
{
	static const u32 little[] = { 300000, 576000, 768000, 1017600, 1248000,
		1324800, 1497600, 1612800, 1708800, 1804800 };
	static const u32 big[] = { 300000, 652800, 806400, 979200, 1094400,
		1209600, 1324800, 1555200, 1708800, 1843200, 1939200, 2169600,
		2208000, 2304000 };
	const u32 *rates = number == 1 ? little : big;
	u32 n = number == 1 ? ARRAY_SIZE(little) : ARRAY_SIZE(big), i;
	memset(&model, 0, sizeof(model));
	memset(&cluster, 0, sizeof(cluster));
	memset(&core, 0, sizeof(core));
	memset(&policy, 0, sizeof(policy));
	memset(table, 0, sizeof(table));
	cases++;
	model.valid = n;
	model.stock_l = number == 1 ? NOTHING_STOCK_PWRCL_LVAL : NOTHING_STOCK_PERFCL_LVAL;
	model.target_l = number == 1 ? NOTHING_OC_PWRCL_LVAL : NOTHING_OC_PERFCL_LVAL;
	cluster.model = core.model = &model;
	cluster.hw.owner = &cluster;
	core.hw.owner = &core;
	core.hw.parent = &cluster.hw;
	core.oc_parent = &cluster;
	cluster.cluster_num = number;
	cluster.osm_table_size = OSM_TABLE_SIZE;
	cluster.oc_stock_entries = n;
	cluster.oc_stock_index = n - 1;
	cluster.oc_stock_khz = rates[n - 1];
	cluster.oc_stock_limit_khz = rates[n - 1];
	cluster.oc_target_khz = model.target_l * (XO_RATE / 1000);
	cluster.oc_row = n;
	cluster.oc_active = true;
	cluster.oc_thermal_blocked = true;
	for (i = 0; i < OSM_TABLE_SIZE; i++) {
		u32 j = i < n ? i : n - 1;
		u32 freq = (j ? 0x40000000 : 0) | ((number == 1 ? 6 : 2) << 16) |
			(i << 8) | (j ? rates[j] / (XO_RATE / 1000) : 0);
		u32 volt = (10 << 16) | (900 + 10 * j);
		model.regs[(FREQ_REG + i * OSM_REG_SIZE) / 4] = freq;
		model.regs[(VOLT_REG + i * OSM_REG_SIZE) / 4] = volt;
		model.regs[(OVERRIDE_REG + i * OSM_REG_SIZE) / 4] = 0x12345678;
		cluster.oc_freq_words[i] = freq;
		cluster.oc_volt_words[i] = volt;
		cluster.osm_table[i].frequency = rates[j] * 1000UL;
		cluster.osm_table[i].virtual_corner = 10;
		cluster.osm_table[i].open_loop_volt = 900 + 10 * j;
	}
	cluster.osm_table[n].frequency = cluster.oc_target_khz * 1000UL;
	memcpy(model.original, model.regs, sizeof(model.regs));
	policy.driver_data = &core;
	policy.cpus = &policy.cpus_storage;
	policy.cur = 300000;
	policy.cpu = number == 1 ? 0 : 6;
	jiffies = 1000;
	nothing_oc_suspended = false;
	fail_allocation = false;
	arch_rate = notify_pre = notify_post = 0;
}

static void resnapshot(void)
{
	u32 i;
	cluster.oc_stock_khz = 0;
	for (i = 0; i < OSM_TABLE_SIZE; i++) {
		u32 word = model.regs[(FREQ_REG + i * OSM_REG_SIZE) / 4];
		u32 khz = nothing_oc_lut_rate(word) / 1000;
		cluster.oc_freq_words[i] = word;
		cluster.oc_volt_words[i] = model.regs[(VOLT_REG + i * OSM_REG_SIZE) / 4];
		if (i < model.valid && CORE_COUNT_VAL(word) != SINGLE_CORE_COUNT &&
		    khz <= cluster.oc_stock_limit_khz &&
		    khz >= cluster.oc_stock_khz) {
			cluster.oc_stock_khz = khz;
			cluster.oc_stock_index = i;
		}
	}
	memcpy(model.original, model.regs, sizeof(model.regs));
}

static void stock_unchanged(void)
{
	u32 i;
	CHECK(model.stock_writes == 0 && model.override_writes == 0);
	for (i = 0; i < model.valid; i++) {
		CHECK(model.regs[(FREQ_REG + i * OSM_REG_SIZE) / 4] ==
		      model.original[(FREQ_REG + i * OSM_REG_SIZE) / 4]);
		CHECK(model.regs[(VOLT_REG + i * OSM_REG_SIZE) / 4] ==
		      model.original[(VOLT_REG + i * OSM_REG_SIZE) / 4]);
	}
}

static void table_and_fallback(void)
{
	CHECK(osm_cpufreq_cpu_init(&policy) == 0);
	CHECK(policy.freq_table[model.valid].frequency == cluster.oc_target_khz);
	CHECK(policy.freq_table[model.valid + 1].frequency == CPUFREQ_TABLE_END);
	CHECK(osm_cpufreq_fast_switch(&policy, cluster.oc_target_khz) == cluster.oc_stock_khz);
	CHECK(model.last_request == cluster.oc_stock_index);
	CHECK(arch_rate == cluster.oc_stock_khz);
	CHECK(osm_cpufreq_target(&policy, cluster.oc_target_khz, CPUFREQ_RELATION_L) == 0);
	CHECK(policy.cur == cluster.oc_stock_khz);
	CHECK(notify_pre == cluster.oc_stock_khz && notify_post == cluster.oc_stock_khz);
	CHECK(osm_cpufreq_get(policy.cpu) == cluster.oc_stock_khz);
	policy.min = policy.max = cluster.oc_target_khz;
	CHECK(osm_cpufreq_verify(&policy) == 0);
	CHECK(policy.min == cluster.oc_stock_khz && policy.max == cluster.oc_stock_khz);
}

static void run_tests(void)
{
	u32 number, fault, i, mv, prev;
	for (number = 1; number <= 2; number++) {
		init(number);
		nothing_oc_dump_lut(&cluster, "mock-firmware");
		CHECK(clk_osm_oc_apply(&cluster, model.valid) == 1);
		CHECK(cluster.oc_hw_verified);
		CHECK(model.lut_writes == 4 && model.freq_writes == 2);
		stock_unchanged();
		table_and_fallback();
		cluster.oc_thermal_blocked = false;
		cluster.oc_thermal_sample = jiffies;
		CHECK(osm_cpufreq_fast_switch(&policy, cluster.oc_target_khz) == cluster.oc_target_khz);
		CHECK(model.last_request == model.valid && arch_rate == cluster.oc_target_khz);
		CHECK(osm_cpufreq_get(policy.cpu) == cluster.oc_target_khz);
		policy.cur = cluster.oc_stock_khz;
		CHECK(osm_cpufreq_target(&policy, cluster.oc_target_khz, CPUFREQ_RELATION_L) == 0);
		CHECK(notify_post == cluster.oc_target_khz && policy.cur == cluster.oc_target_khz);
		jiffies += NOTHING_OC_STALE_MS + 1;
		CHECK(!nothing_oc_can_request(&cluster));
		CHECK(osm_cpufreq_fast_switch(&policy, cluster.oc_target_khz) == cluster.oc_stock_khz);
		cluster.oc_thermal_sample = jiffies;
		nothing_oc_suspended = true;
		CHECK(osm_cpufreq_fast_switch(&policy, cluster.oc_target_khz) == cluster.oc_stock_khz);
		nothing_oc_suspended = false;
		cluster.oc_thermal_sample = ~0UL - 20;
		jiffies = 10;
		CHECK(nothing_oc_can_request(&cluster));
		jiffies = 600;
		CHECK(!nothing_oc_can_request(&cluster));
		CHECK(osm_cpufreq_cpu_exit(&policy) == 0 && policy.freq_table == NULL);

		for (fault = REJECT_ALL; fault <= FAIL_ROLLBACK; fault++) {
			init(number);
			model.fault = fault;
			CHECK(clk_osm_oc_apply(&cluster, model.valid) == 0);
			CHECK(!cluster.oc_hw_verified);
			stock_unchanged();
			table_and_fallback();
			/* A synthetic table entry is not a hardware frequency. */
			model.regs[DCVS_PERF_STATE_DESIRED_REG(0) / 4] = model.valid;
			if (fault != FAIL_ROLLBACK)
				CHECK(osm_cpufreq_get(policy.cpu) == cluster.oc_stock_khz);
		}
		for (fault = REJECT_REQUEST; fault <= CHANGE_ON_REQUEST; fault++) {
			init(number);
			CHECK(clk_osm_oc_apply(&cluster, model.valid) == 1);
			CHECK(osm_cpufreq_cpu_init(&policy) == 0);
			cluster.oc_thermal_blocked = false;
			cluster.oc_thermal_sample = jiffies;
			model.fault = fault;
			CHECK(osm_cpufreq_target(&policy, cluster.oc_target_khz, CPUFREQ_RELATION_L) == 0);
			CHECK(!cluster.oc_hw_verified);
			CHECK(notify_post == cluster.oc_stock_khz && policy.cur == cluster.oc_stock_khz);
			CHECK(arch_rate == cluster.oc_stock_khz && model.last_request == cluster.oc_stock_index);
			stock_unchanged();
		}
		for (i = 0; i < 3; i++) {
			init(number);
			CHECK(clk_osm_oc_apply(&cluster, model.valid) == 1);
			CHECK(osm_cpufreq_cpu_init(&policy) == 0);
			cluster.oc_thermal_blocked = false;
			cluster.oc_thermal_sample = jiffies;
			model.regs[((i == 0 ? FREQ_REG : i == 1 ? VOLT_REG : OVERRIDE_REG) +
				model.valid * OSM_REG_SIZE) / 4] ^= 1;
			CHECK(osm_cpufreq_fast_switch(&policy, cluster.oc_target_khz) == cluster.oc_stock_khz);
			CHECK(!cluster.oc_hw_verified && model.last_request == cluster.oc_stock_index);
		}
		/* Every active per-core desired register must prohibit tail programming. */
		for (i = 0; i < (number == 1 ? 6U : 2U); i++) {
			init(number);
			cluster.per_core_dcvs = true;
			model.regs[DCVS_PERF_STATE_DESIRED_REG(i) / 4] = model.valid;
			CHECK(clk_osm_oc_apply(&cluster, model.valid) == 0);
			CHECK(model.lut_writes == 0);
			table_and_fallback();
		}
		/* Metadata, duplicate, voltage and slope guards. */
		for (i = 0; i < 14; i++) {
			u32 off, value;
			init(number);
			off = FREQ_REG + (model.valid - 1) * OSM_REG_SIZE;
			value = model.regs[off / 4];
			switch (i) {
			case 0: model.regs[off / 4] ^= 1; break;
			case 1: model.regs[off / 4] ^= 1U << 30; break;
			case 2: model.regs[off / 4] ^= 1U << 28; break;
			case 3: model.regs[off / 4] = (value & ~GENMASK(18, 16)) | (1U << 16); break;
			case 4: model.regs[off / 4] ^= 1U << 8; break;
			case 5: model.regs[(FREQ_REG + model.valid * OSM_REG_SIZE) / 4] ^= 1; break;
			case 6: model.regs[(VOLT_REG + model.valid * OSM_REG_SIZE) / 4] ^= 1; break;
			case 7: model.regs[(OVERRIDE_REG + model.valid * OSM_REG_SIZE) / 4] ^= 1; break;
			case 8: model.regs[(FREQ_REG + (model.valid + 1) * OSM_REG_SIZE) / 4] ^= 1U << 8; break;
			case 9:
			case 10:
			case 11:
				for (prev = model.valid - 1; prev <= model.valid + 1; prev++)
					model.regs[(VOLT_REG + prev * OSM_REG_SIZE) / 4] =
						(i == 9 ? (61U << 16) | 1000 :
						 i == 10 ? (10U << 16) | 399 : (10U << 16) | 1299);
				break;
			case 12: model.regs[(VOLT_REG + (model.valid - 2) * OSM_REG_SIZE) / 4] |= 0xfff; break;
			case 13: model.regs[(FREQ_REG + (model.valid - 2) * OSM_REG_SIZE) / 4] ^= 1U << 30; break;
			}
			resnapshot();
			CHECK(clk_osm_oc_apply(&cluster, model.valid) == 0);
			CHECK(model.lut_writes == 0 && !cluster.oc_hw_verified);
			table_and_fallback();
		}
		init(number);
		CHECK(clk_osm_oc_apply(&cluster, 1) == 0 && model.lut_writes == 0);
		CHECK(clk_osm_oc_apply(&cluster, 39) == 0 && model.lut_writes == 0);
		cluster.oc_active = false;
		CHECK(clk_osm_oc_apply(&cluster, model.valid) == 0 && model.lut_writes == 0);
		CHECK(osm_cpufreq_cpu_init(&policy) == 0);
		CHECK(policy.freq_table[model.valid].frequency == CPUFREQ_TABLE_END);
		init(number);
		fail_allocation = true;
		CHECK(osm_cpufreq_cpu_init(&policy) == -ENOMEM);
		init(number);
		model.fault = DIVERGE_STOCK;
		CHECK(clk_osm_oc_apply(&cluster, model.valid) == 0);
		CHECK(!cluster.oc_hw_verified && model.stock_writes == 0);
	}
	for (mv = 400; mv < 1300; mv += 7) {
		for (prev = 400; prev <= mv; prev += 11) {
			int step = nothing_oc_voltage_step(94, 89, 110, mv, prev);
			cases++;
			CHECK(step == -EINVAL || (step >= 4 && step <= 8 && mv + step <= 1300));
		}
	}
	CHECK(nothing_oc_voltage_step(94, 94, 110, 1000, 990) == -EINVAL);
	CHECK(nothing_oc_voltage_step(94, 89, 94, 1000, 990) == -EINVAL);
	CHECK(nothing_oc_voltage_step(94, 89, 256, 1000, 990) == -EINVAL);
	CHECK(nothing_oc_voltage_step(94, 89, 110, 1000, 1001) == -EINVAL);
	CHECK(nothing_oc_voltage_step(94, 89, 110, 1000, 1000) == 4);
	CHECK(nothing_oc_voltage_step(94, 89, 110, 1000, 990) == 8);
	for (i = 0; i < 2; i++) {
		cases++;
		CHECK(nothing_oc_temperature_blocked(i, false, 40000, 35000, 70000, 60000, 42000, 39000));
		CHECK(nothing_oc_temperature_blocked(i, true, -274000, 35000, 70000, 60000, 42000, 39000));
		CHECK(nothing_oc_temperature_blocked(i, true, 40000, -1, 70000, 60000, 42000, 39000));
		CHECK(nothing_oc_temperature_blocked(i, true, 70000, 35000, 70000, 60000, 42000, 39000));
		CHECK(nothing_oc_temperature_blocked(i, true, 40000, 42000, 70000, 60000, 42000, 39000));
		CHECK(!nothing_oc_temperature_blocked(i, true, 60000, 39000, 70000, 60000, 42000, 39000));
	}
	CHECK(nothing_oc_temperature_blocked(true, true, 60001, 39000, 70000, 60000, 42000, 39000));
	CHECK(nothing_oc_temperature_blocked(true, true, 60000, 39001, 70000, 60000, 42000, 39000));
	CHECK(!nothing_oc_temperature_blocked(false, true, 69999, 41999, 70000, 60000, 42000, 39000));
	CHECK(log_count > 0);
}

#ifndef __aarch64__
int main(void)
{
	run_tests();
	printf("Nothing OC1 mock MMIO/thermal/CPUFreq: PASS (%u cases, %u assertions; ASan/UBSan)\n",
	       cases, assertions);
	return 0;
}
#else
void _start(void)
{
	static const char message[] = "Nothing OC1 AArch64-emulated MMIO/thermal/CPUFreq: PASS\n";
	run_tests();
	output(message, sizeof(message) - 1);
	finish(0);
}
#endif
