// SPDX-License-Identifier: GPL-2.0
//
// simple gpio switch support
// use one gpio to control gpios
// this really should be a generic gpio switch driver, leave it here anyway.
//
// Copyright (C) 2023 PotatoMania <nikko@faint.day>
//

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
// #include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

/* TODO: support regulators? */
struct simple_amplifier_switch {
	struct gpio_desc *sw;
	struct gpio_descs *outputs;
};

// Components:
// sw-gpios: 1 switch input
// outputs-gpios: array of gpios

static inline void set_outputs(struct gpio_descs *outputs, int value)
{
	unsigned long *values;
	int nvalues = outputs->ndescs;

	values = bitmap_alloc(nvalues, GFP_KERNEL);
	if (!values)
		return;

	if (value)
		bitmap_fill(values, nvalues);
	else
		bitmap_zero(values, nvalues);

	gpiod_set_array_value_cansleep(nvalues, outputs->desc,
							outputs->info, values);

	bitmap_free(values);
}

static irqreturn_t amplifier_switch_interrupt(int irq, void *data)
{
	struct simple_amplifier_switch *ampsw = data;
	int state;

	state = gpiod_get_value(ampsw->sw);
	set_outputs(ampsw->outputs, state);

	return IRQ_HANDLED;
}

static int amplifier_switch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simple_amplifier_switch *ampsw;
	int current_state;
	int err;

	ampsw = devm_kzalloc(dev, sizeof(*ampsw), GFP_KERNEL);
	if (!ampsw)
		return -ENOMEM;

	ampsw->sw = devm_gpiod_get(dev, "sw", GPIOD_IN);
	if (IS_ERR(ampsw->sw)) {
		err = PTR_ERR(ampsw->sw);
		dev_err(dev, "Failed to get sw gpio! The input is required! (%d)\n", err);
		return err;
	}

	ampsw->outputs = devm_gpiod_get_array(dev, "outputs", GPIOD_OUT_LOW);
	if (IS_ERR(ampsw->outputs)) {
		err = PTR_ERR(ampsw->outputs);
		dev_err(dev, "Failed to get outputs gpios! (%d)\n", err);
		return err;
	}

	// setup initial state
	current_state = gpiod_get_value(ampsw->sw);
	set_outputs(ampsw->outputs, current_state);

	// register interrupts
	err = devm_request_irq(dev, gpiod_to_irq(ampsw->sw), amplifier_switch_interrupt,
						IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "amplifier-switch", ampsw);
	if (err) {
		dev_err(dev, "Failed to request interrupt (%d)\n", err);
		return err;
	}

	platform_set_drvdata(pdev, ampsw);

	return 0;
}

static void amplifier_switch_shutdown(struct platform_device *pdev)
{
	struct simple_amplifier_switch *ampsw = platform_get_drvdata(pdev);

	// Unregister interrupt
	devm_free_irq(&pdev->dev, gpiod_to_irq(ampsw->sw), ampsw);

	// Turn off all outputs
	set_outputs(ampsw->outputs, 0);
}

static const struct of_device_id of_amplifier_switchs_match[] = {
	{ .compatible = "simple-amplifier-switch" },
	{/* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_amplifier_switchs_match);

static struct platform_driver amplifier_switch_driver = {
	.probe		= amplifier_switch_probe,
	.shutdown	= amplifier_switch_shutdown,
	.driver		= {
		.name	= "simple-amplifier-switch",
		.of_match_table = of_amplifier_switchs_match,
	},
};

module_platform_driver(amplifier_switch_driver);

MODULE_AUTHOR("PotatoMania <nikko@faint.day>");
MODULE_DESCRIPTION("A simple GPIO controlled gpios switch");
MODULE_LICENSE("GPL v2");