/*
 * Generic OPP helper interface for CPU device
 *
 * Copyright (C) 2009-2014 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "opp.h"

#ifdef CONFIG_CPU_FREQ

/**
 * dev_pm_opp_init_cpufreq_table() - create a cpufreq table for a device
 * @dev:	device for which we do this operation
 * @table:	Cpufreq table returned back to caller
 *
 * Generate a cpufreq table for a provided device- this assumes that the
 * opp table is already initialized and ready for usage.
 *
 * This function allocates required memory for the cpufreq table. It is
 * expected that the caller does the required maintenance such as freeing
 * the table as required.
 *
 * Returns -EINVAL for bad pointers, -ENODEV if the device is not found, -ENOMEM
 * if no memory available for the operation (table is not populated), returns 0
 * if successful and table is populated.
 *
 * WARNING: It is  important for the callers to ensure refreshing their copy of
 * the table if any of the mentioned functions have been invoked in the interim.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Since we just use the regular accessor functions to access the internal data
 * structures, we use RCU read lock inside this function. As a result, users of
 * this function DONOT need to use explicit locks for invoking.
 */
int dev_pm_opp_init_cpufreq_table(struct device *dev,
				  struct cpufreq_frequency_table **table)
{
	struct dev_pm_opp *opp;
	struct cpufreq_frequency_table *freq_table = NULL;
	int i, max_opps, ret = 0;
	unsigned long rate;

	rcu_read_lock();

	max_opps = dev_pm_opp_get_opp_count(dev);
	if (max_opps <= 0) {
		ret = max_opps ? max_opps : -ENODATA;
		goto out;
	}

	freq_table = kcalloc((max_opps + 1), sizeof(*freq_table), GFP_ATOMIC);
	if (!freq_table) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0, rate = 0; i < max_opps; i++, rate++) {
		/* find next rate */
		opp = dev_pm_opp_find_freq_ceil(dev, &rate);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto out;
		}
		freq_table[i].driver_data = i;
		freq_table[i].frequency = rate / 1000;

		/* Is Boost/turbo opp ? */
		if (dev_pm_opp_is_turbo(opp))
			freq_table[i].flags = CPUFREQ_BOOST_FREQ;
	}

	freq_table[i].driver_data = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	*table = &freq_table[0];

out:
	rcu_read_unlock();
	if (ret)
		kfree(freq_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_init_cpufreq_table);

/**
 * dev_pm_opp_free_cpufreq_table() - free the cpufreq table
 * @dev:	device for which we do this operation
 * @table:	table to free
 *
 * Free up the table allocated by dev_pm_opp_init_cpufreq_table
 */
void dev_pm_opp_free_cpufreq_table(struct device *dev,
				   struct cpufreq_frequency_table **table)
{
	if (!table)
		return;

	kfree(*table);
	*table = NULL;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_free_cpufreq_table);
#endif	/* CONFIG_CPU_FREQ */

static void
_dev_pm_opp_cpumask_remove_table(const struct cpumask *cpumask, bool of)
{
	struct device *cpu_dev;
	int cpu;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		if (of)
			dev_pm_opp_of_remove_table(cpu_dev);
		else
			dev_pm_opp_remove_table(cpu_dev);
	}
}

/**
 * dev_pm_opp_cpumask_remove_table() - Removes OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be removed
 *
 * This removes the OPP tables for CPUs present in the @cpumask.
 * This should be used to remove all the OPPs entries associated with
 * the cpus in @cpumask.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_cpumask_remove_table(const struct cpumask *cpumask)
{
	_dev_pm_opp_cpumask_remove_table(cpumask, false);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_cpumask_remove_table);

#ifdef CONFIG_OF
/**
 * dev_pm_opp_of_cpumask_remove_table() - Removes OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be removed
 *
 * This removes the OPP tables for CPUs present in the @cpumask.
 * This should be used only to remove static entries created from DT.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_of_cpumask_remove_table(const struct cpumask *cpumask)
{
	_dev_pm_opp_cpumask_remove_table(cpumask, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_remove_table);

/**
 * dev_pm_opp_of_cpumask_add_table() - Adds OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be added.
 *
 * This adds the OPP tables for CPUs present in the @cpumask.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
int dev_pm_opp_of_cpumask_add_table(const struct cpumask *cpumask)
{
	struct device *cpu_dev;
	int cpu, ret = 0;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		ret = dev_pm_opp_of_add_table(cpu_dev);
		if (ret) {
			pr_err("%s: couldn't find opp table for cpu:%d, %d\n",
			       __func__, cpu, ret);

			/* Free all other OPPs */
			dev_pm_opp_of_cpumask_remove_table(cpumask);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_add_table);

/*
 * Works only for OPP v2 bindings.
 *
 * Returns -ENOENT if operating-points-v2 bindings aren't supported.
 */
/**
 * dev_pm_opp_of_get_sharing_cpus() - Get cpumask of CPUs sharing OPPs with
 *				      @cpu_dev using operating-points-v2
 *				      bindings.
 *
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask to update with information of sharing CPUs
 *
 * This updates the @cpumask with CPUs that are sharing OPPs with @cpu_dev.
 *
 * Returns -ENOENT if operating-points-v2 isn't present for @cpu_dev.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	struct device_node *np, *tmp_np;
	struct device *tcpu_dev;
	int cpu, ret = 0;

	/* Get OPP descriptor node */
	np = _of_get_opp_desc_node(cpu_dev);
	if (!np) {
		dev_dbg(cpu_dev, "%s: Couldn't find cpu_dev node.\n", __func__);
		return -ENOENT;
	}

	cpumask_set_cpu(cpu_dev->id, cpumask);

	/* OPPs are shared ? */
	if (!of_property_read_bool(np, "opp-shared"))
		goto put_cpu_node;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		tcpu_dev = get_cpu_device(cpu);
		if (!tcpu_dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			ret = -ENODEV;
			goto put_cpu_node;
		}

		/* Get OPP descriptor node */
		tmp_np = _of_get_opp_desc_node(tcpu_dev);
		if (!tmp_np) {
			dev_err(tcpu_dev, "%s: Couldn't find tcpu_dev node.\n",
				__func__);
			ret = -ENOENT;
			goto put_cpu_node;
		}

		/* CPUs are sharing opp node */
		if (np == tmp_np)
			cpumask_set_cpu(cpu, cpumask);

		of_node_put(tmp_np);
	}

put_cpu_node:
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_get_sharing_cpus);
#endif

/**
 * dev_pm_opp_set_sharing_cpus() - Mark OPP table as shared by few CPUs
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask of the CPUs which share the OPP table with @cpu_dev
 *
 * This marks OPP table of the @cpu_dev as shared by the CPUs present in
 * @cpumask.
 *
 * Returns -ENODEV if OPP table isn't already present.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev,
				const struct cpumask *cpumask)
{
	struct opp_device *opp_dev;
	struct opp_table *opp_table;
	struct device *dev;
	int cpu, ret = 0;

	mutex_lock(&opp_table_lock);

	opp_table = _find_opp_table(cpu_dev);
	if (IS_ERR(opp_table)) {
		ret = PTR_ERR(opp_table);
		goto unlock;
	}

	for_each_cpu(cpu, cpumask) {
		if (cpu == cpu_dev->id)
			continue;

		dev = get_cpu_device(cpu);
		if (!dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			continue;
		}

		opp_dev = _add_opp_dev(dev, opp_table);
		if (!opp_dev) {
			dev_err(dev, "%s: failed to add opp-dev for cpu%d device\n",
				__func__, cpu);
			continue;
		}

		/* Mark opp-table as multiple CPUs are sharing it now */
		opp_table->shared_opp = true;
	}
unlock:
	mutex_unlock(&opp_table_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_set_sharing_cpus);

/**
 * dev_pm_opp_get_sharing_cpus() - Get cpumask of CPUs sharing OPPs with @cpu_dev
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask to update with information of sharing CPUs
 *
 * This updates the @cpumask with CPUs that are sharing OPPs with @cpu_dev.
 *
 * Returns -ENODEV if OPP table isn't already present.
 *
 * Locking: The internal opp_table and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
int dev_pm_opp_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	struct opp_device *opp_dev;
	struct opp_table *opp_table;
	int ret = 0;

	mutex_lock(&opp_table_lock);

	opp_table = _find_opp_table(cpu_dev);
	if (IS_ERR(opp_table)) {
		ret = PTR_ERR(opp_table);
		goto unlock;
	}

	cpumask_clear(cpumask);

	if (opp_table->shared_opp) {
		list_for_each_entry(opp_dev, &opp_table->dev_list, node)
			cpumask_set_cpu(opp_dev->dev->id, cpumask);
	} else {
		cpumask_set_cpu(cpu_dev->id, cpumask);
	}

unlock:
	mutex_unlock(&opp_table_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_sharing_cpus);
