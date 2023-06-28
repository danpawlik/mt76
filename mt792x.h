/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2023 MediaTek Inc. */

#ifndef __MT792X_H
#define __MT792X_H

#include <linux/interrupt.h>
#include <linux/ktime.h>

#include "mt76_connac_mcu.h"
#include "mt792x_regs.h"

#define MT792x_MAX_INTERFACES	4
#define MT792x_WTBL_SIZE	20
#define MT792x_WTBL_RESERVED	(MT792x_WTBL_SIZE - 1)
#define MT792x_WTBL_STA		(MT792x_WTBL_RESERVED - MT792x_MAX_INTERFACES)

#define MT792x_CFEND_RATE_DEFAULT	0x49	/* OFDM 24M */
#define MT792x_CFEND_RATE_11B		0x03	/* 11B LP, 11M */

/* NOTE: used to map mt76_rates. idx may change if firmware expands table */
#define MT792x_BASIC_RATES_TBL	11

#define MT792x_WATCHDOG_TIME	(HZ / 4)

struct mt792x_vif;
struct mt792x_sta;

enum {
	MT792x_CLC_POWER,
	MT792x_CLC_CHAN,
	MT792x_CLC_MAX_NUM,
};

DECLARE_EWMA(avg_signal, 10, 8)

struct mt792x_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt792x_vif *vif;

	u32 airtime_ac[8];

	int ack_signal;
	struct ewma_avg_signal avg_ack_signal;

	unsigned long last_txs;

	struct mt76_connac_sta_key_conf bip;
};

DECLARE_EWMA(rssi, 10, 8);

struct mt792x_vif {
	struct mt76_vif mt76; /* must be first */

	struct mt792x_sta sta;
	struct mt792x_sta *wep_sta;

	struct mt792x_phy *phy;

	struct ewma_rssi rssi;

	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
	struct ieee80211_chanctx_conf *ctx;
};

struct mt792x_phy {
	struct mt76_phy *mt76;
	struct mt792x_dev *dev;

	struct ieee80211_sband_iftype_data iftype[NUM_NL80211_BANDS][NUM_NL80211_IFTYPES];

	u64 omac_mask;

	u16 noise;

	s16 coverage_class;
	u8 slottime;

	u32 rx_ampdu_ts;
	u32 ampdu_ref;

	struct mt76_mib_stats mib;

	u8 sta_work_count;

	struct sk_buff_head scan_event_list;
	struct delayed_work scan_work;
#ifdef CONFIG_ACPI
	void *acpisar;
#endif
	void *clc[MT792x_CLC_MAX_NUM];

	struct work_struct roc_work;
	struct timer_list roc_timer;
	wait_queue_head_t roc_wait;
	u8 roc_token_id;
	bool roc_grant;
};

struct mt792x_hif_ops {
	int (*init_reset)(struct mt792x_dev *dev);
	int (*reset)(struct mt792x_dev *dev);
	int (*mcu_init)(struct mt792x_dev *dev);
	int (*drv_own)(struct mt792x_dev *dev);
	int (*fw_own)(struct mt792x_dev *dev);
};

struct mt792x_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	const struct mt76_bus_ops *bus_ops;
	struct mt792x_phy phy;

	struct work_struct reset_work;
	bool hw_full_reset:1;
	bool hw_init_done:1;
	bool fw_assert:1;
	bool has_eht:1;

	struct work_struct init_work;

	u8 fw_debug;
	u8 fw_features;

	struct mt76_connac_pm pm;
	struct mt76_connac_coredump coredump;
	const struct mt792x_hif_ops *hif_ops;

	struct work_struct ipv6_ns_work;
	/* IPv6 addresses for WoWLAN */
	struct sk_buff_head ipv6_ns_list;

	enum environment_cap country_ie_env;
	u32 backup_l1;
	u32 backup_l2;
};

static inline struct mt792x_dev *
mt792x_hw_dev(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return container_of(phy->dev, struct mt792x_dev, mt76);
}

static inline struct mt792x_phy *
mt792x_hw_phy(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return phy->priv;
}

static inline void
mt792x_get_status_freq_info(struct mt76_rx_status *status, u8 chfreq)
{
	if (chfreq > 180) {
		status->band = NL80211_BAND_6GHZ;
		chfreq = (chfreq - 181) * 4 + 1;
	} else if (chfreq > 14) {
		status->band = NL80211_BAND_5GHZ;
	} else {
		status->band = NL80211_BAND_2GHZ;
	}
	status->freq = ieee80211_channel_to_frequency(chfreq, status->band);
}

static inline bool mt792x_dma_need_reinit(struct mt792x_dev *dev)
{
	return !mt76_get_field(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);
}

#define mt792x_mutex_acquire(dev)	\
	mt76_connac_mutex_acquire(&(dev)->mt76, &(dev)->pm)
#define mt792x_mutex_release(dev)	\
	mt76_connac_mutex_release(&(dev)->mt76, &(dev)->pm)

void mt792x_reset(struct mt76_dev *mdev);
void mt792x_update_channel(struct mt76_phy *mphy);
void mt792x_mac_reset_counters(struct mt792x_phy *phy);
void mt792x_mac_assoc_rssi(struct mt792x_dev *dev, struct sk_buff *skb);
struct mt76_wcid *mt792x_rx_get_wcid(struct mt792x_dev *dev, u16 idx,
				     bool unicast);
void mt792x_mac_update_mib_stats(struct mt792x_phy *phy);
void mt792x_mac_set_timeing(struct mt792x_phy *phy);
void mt792x_mac_work(struct work_struct *work);
void mt792x_remove_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
void mt792x_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	       struct sk_buff *skb);
int mt792x_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   unsigned int link_id, u16 queue,
		   const struct ieee80211_tx_queue_params *params);
int mt792x_get_stats(struct ieee80211_hw *hw,
		     struct ieee80211_low_level_stats *stats);
u64 mt792x_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void mt792x_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u64 timestamp);
void mt792x_tx_worker(struct mt76_worker *w);
void mt792x_roc_timer(struct timer_list *timer);
void mt792x_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop);
int mt792x_assign_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf,
			      struct ieee80211_chanctx_conf *ctx);
void mt792x_unassign_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 struct ieee80211_chanctx_conf *ctx);
void mt792x_set_wakeup(struct ieee80211_hw *hw, bool enabled);
void mt792x_get_et_strings(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   u32 sset, u8 *data);
int mt792x_get_et_sset_count(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     int sset);
void mt792x_get_et_stats(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data);
void mt792x_sta_statistics(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct station_info *sinfo);
void mt792x_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class);
void mt792x_dma_cleanup(struct mt792x_dev *dev);
int mt792x_dma_disable(struct mt792x_dev *dev, bool force);
int mt792x_poll_rx(struct napi_struct *napi, int budget);
int mt792x_wfsys_reset(struct mt792x_dev *dev, u32 addr);
int mt792x_tx_stats_show(struct seq_file *file, void *data);
int mt792x_queues_acq(struct seq_file *s, void *data);
int mt792x_queues_read(struct seq_file *s, void *data);
int mt792x_pm_stats(struct seq_file *s, void *data);
int mt792x_pm_idle_timeout_set(void *data, u64 val);
int mt792x_pm_idle_timeout_get(void *data, u64 *val);

#endif /* __MT7925_H */