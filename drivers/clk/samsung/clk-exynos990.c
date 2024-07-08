// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/exynos990.h>

#include "clk.h"
#include "clk-cpu.h"
#include "clk-exynos-arm64.h"

#define CLKS_NR_AUD			(CLK_GOUT_AUD_DMIC1 + 1)

/* ---------- CMU_AUD (0x18c00000) ---------- */
#define PLL_LOCKTIME_PLL_AUD0			0x00
#define PLL_LOCKTIME_PLL_AUD1			0x04
#define PLL_CON3_PLL_AUD0			0x10c
#define PLL_CON5_PLL_AUD0			0x114
#define PLL_CON3_PLL_AUD1			0x14c
#define PLL_CON5_PLL_AUD1			0x154
#define PLL_CON0_MUX_CLKCMU_AUD_CPU_USER	0x600
#define PLL_CON1_MUX_CLKCMU_AUD_CPU_USER	0x604
#define CLK_CON_MUX_MUX_CLK_AUD_CNT		0x1000
#define CLK_CON_MUX_MUX_CLK_AUD_CPU		0x1004
#define CLK_CON_MUX_MUX_CLK_AUD_DSIF		0x1008
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF0		0x100c
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF1		0x1010
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF2		0x1014
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF3		0x1018
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF4		0x101c
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF5		0x1020
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF6		0x1024
#define CLK_CON_MUX_MUX_HCHGEN_CLK_AUD_CPU	0x1028
#define CLK_CON_DIV_DIV_CLK_AUD_AUDIF		0x1800
#define CLK_CON_DIV_DIV_CLK_AUD_BUS		0x1804
#define CLK_CON_DIV_DIV_CLK_AUD_BUSP		0x1808
#define CLK_CON_DIV_DIV_CLK_AUD_CNT		0x180c
#define CLK_CON_DIV_DIV_CLK_AUD_CPU_ACLK	0x1810
#define CLK_CON_DIV_DIV_CLK_AUD_CPU_PCLKDBG	0x1814
#define CLK_CON_DIV_DIV_CLK_AUD_DMIC0		0x1818
#define CLK_CON_DIV_DIV_CLK_AUD_DMIC1		0x181c
#define CLK_CON_DIV_DIV_CLK_AUD_DSIF		0x1820
#define CLK_CON_DIV_DIV_CLK_AUD_PLL		0x1824
#define CLK_CON_DIV_DIV_CLK_AUD_SCLK		0x1828
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF0		0x182c
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF1		0x1830
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF2		0x1834
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF3		0x1838
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF4		0x183c
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF5		0x1840
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF6		0x1844
#define CLK_CON_GAT_CLK_AUD_DMIC0		0x2000
#define CLK_CON_GAT_CLK_AUD_DMIC1		0x2004

static const unsigned long aud_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUD0,
	PLL_LOCKTIME_PLL_AUD1,
	PLL_CON3_PLL_AUD0,
	PLL_CON5_PLL_AUD0,
	PLL_CON3_PLL_AUD1,
	PLL_CON5_PLL_AUD1,
	PLL_CON0_MUX_CLKCMU_AUD_CPU_USER,
	PLL_CON1_MUX_CLKCMU_AUD_CPU_USER,
	CLK_CON_MUX_MUX_CLK_AUD_CNT,
	CLK_CON_MUX_MUX_CLK_AUD_CPU,
	CLK_CON_MUX_MUX_CLK_AUD_DSIF,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF0,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF1,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF2,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF3,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF4,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF5,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF6,
	CLK_CON_MUX_MUX_HCHGEN_CLK_AUD_CPU,
	CLK_CON_DIV_DIV_CLK_AUD_AUDIF,
	CLK_CON_DIV_DIV_CLK_AUD_BUS,
	CLK_CON_DIV_DIV_CLK_AUD_BUSP,
	CLK_CON_DIV_DIV_CLK_AUD_CNT,
	CLK_CON_DIV_DIV_CLK_AUD_CPU_ACLK,
	CLK_CON_DIV_DIV_CLK_AUD_CPU_PCLKDBG,
	CLK_CON_DIV_DIV_CLK_AUD_DMIC0,
	CLK_CON_DIV_DIV_CLK_AUD_DMIC1,
	CLK_CON_DIV_DIV_CLK_AUD_DSIF,
	CLK_CON_DIV_DIV_CLK_AUD_PLL,
	CLK_CON_DIV_DIV_CLK_AUD_SCLK,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF0,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF1,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF2,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF3,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF4,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF5,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF6,
	CLK_CON_GAT_CLK_AUD_DMIC0,
	CLK_CON_GAT_CLK_AUD_DMIC1
};

/* Parent names for AUD muxes */
PNAME(mout_aud_pll_p)		= { "oscclk", "fout_aud_pll" };
PNAME(mout_aud_cnt_p) 		= { "dout_aud_audif", "fout_aud1_pll" };
PNAME(mout_aud_cpu_p) 		= { "dout_aud_cpu", "mout_aud_cpu_user" };
PNAME(mout_aud_cpu_user_p)	= { "oscclk", "dout_aud" };
PNAME(mout_hchgen_clk_aud_cpu_p) = { "mout_clk_aud_cpu", "oscclk" };
PNAME(mout_aud_uaif0_p)		= { "dout_aud_uaif0", "ioclk_audiocdclk0" };
PNAME(mout_aud_uaif1_p)		= { "dout_aud_uaif1", "ioclk_audiocdclk1" };
PNAME(mout_aud_uaif2_p)		= { "dout_aud_uaif2", "ioclk_audiocdclk2" };
PNAME(mout_aud_uaif3_p)		= { "dout_aud_uaif3", "ioclk_audiocdclk3" };
PNAME(mout_aud_uaif4_p)		= { "dout_aud_uaif4", "ioclk_audiocdclk4" };
PNAME(mout_aud_uaif5_p)		= { "dout_aud_uaif5", "ioclk_audiocdclk5" };
PNAME(mout_aud_uaif6_p)		= { "dout_aud_uaif6", "ioclk_audiocdclk6" };


/* AUD doesn't have input PLLs. */
static const struct samsung_pll_clock aud_pll_clks[] __initconst = {
	PLL(pll_0732x, CLK_FOUT_AUD0_PLL, "fout_aud0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_AUD0, PLL_CON3_PLL_AUD0, NULL),
	PLL(pll_0732x, CLK_FOUT_AUD1_PLL, "fout_aud1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_AUD1, PLL_CON3_PLL_AUD1, NULL),
};

static const struct samsung_fixed_rate_clock aud_fixed_clks[] __initconst = {
};

static const struct samsung_mux_clock aud_mux_clks[] __initconst = {

	MUX(CLK_MOUT_AUD_CPU, "mout_aud_cpu", mout_aud_cpu_p,
	    CLK_CON_MUX_MUX_CLK_AUD_CPU, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF0, "mout_aud_uaif0", mout_aud_uaif0_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF0, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF1, "mout_aud_uaif1", mout_aud_uaif1_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF1, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF2, "mout_aud_uaif2", mout_aud_uaif2_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF2, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF3, "mout_aud_uaif3", mout_aud_uaif3_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF3, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF4, "mout_aud_uaif4", mout_aud_uaif4_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF4, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF5, "mout_aud_uaif5", mout_aud_uaif5_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF5, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF6, "mout_aud_uaif6", mout_aud_uaif6_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF6, 0, 1),
};

static const struct samsung_div_clock aud_div_clks[] __initconst = {
	DIV(CLK_DOUT_AUD_CPU, "dout_aud_cpu", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_PLL, 4, 1),
	DIV(CLK_DOUT_AUD_BUSP, "dout_aud_busp", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_BUSP, 0, 4),
	DIV(CLK_DOUT_AUD_AUDIF, "dout_aud_audif", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_AUDIF, 0, 9),
	DIV(CLK_DOUT_AUD_CPU_ACLK, "dout_aud_cpu_aclk", "mout_aud_cpu_hch",
	    CLK_CON_DIV_DIV_CLK_AUD_CPU_ACLK, 0, 3),
	DIV(CLK_DOUT_AUD_CPU_PCLKDBG, "dout_aud_cpu_pclkdbg",
	    "mout_aud_cpu_hch",
	    CLK_CON_DIV_DIV_CLK_AUD_CPU_PCLKDBG, 0, 3),
	DIV(CLK_DOUT_AUD_CNT, "dout_aud_cnt", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_CNT, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF0, "dout_aud_uaif0", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF0, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF1, "dout_aud_uaif1", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF1, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF2, "dout_aud_uaif2", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF2, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF3, "dout_aud_uaif3", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF3, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF4, "dout_aud_uaif4", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF4, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF5, "dout_aud_uaif5", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF5, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF6, "dout_aud_uaif6", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF6, 0, 10),
};

static const struct samsung_gate_clock aud_gate_clks[] __initconst = {
	GATE(CLK_GOUT_AUD_DMIC0, "gout_aud_dmic0", "fout_aud0_pll",
	     CLK_CON_GAT_CLK_AUD_DMIC0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_AUD_DMIC1, "gout_aud_dmic0", "fout_aud1_pll",
	     CLK_CON_GAT_CLK_AUD_DMIC0, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info aud_cmu_info __initconst = {
	.pll_clks		= aud_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(aud_pll_clks),
	.mux_clks		= aud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(aud_mux_clks),
	.div_clks		= aud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(aud_div_clks),
	.gate_clks		= aud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(aud_gate_clks),
	.fixed_clks		= aud_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(aud_fixed_clks),
	.nr_clk_ids		= CLKS_NR_AUD,
	.clk_regs		= aud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(aud_clk_regs),
	.clk_name		= "dout_aud",
};

/* ----- platform_driver ----- */

static int __init exynos990_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);
	dev_err(&pdev->dev, "Initialized sexynos 990 clk driver\n");

	return 0;
}

static const struct of_device_id exynos990_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos990-cmu-aud",
		.data = &aud_cmu_info,
	}, {
	},
};

static struct platform_driver exynos990_cmu_driver __refdata = {
	.driver	= {
		.name = "exynos990-cmu",
		.of_match_table = exynos990_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos990_cmu_probe,
};

static int __init exynos990_cmu_init(void)
{
	return platform_driver_register(&exynos990_cmu_driver);
}

core_initcall(exynos990_cmu_init);
