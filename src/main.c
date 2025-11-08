#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/sntp.h>
#include <zbus/zbus.h>
#include <time.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* ZBus channel definition */
struct time_msg {
	struct tm time;
};

ZBUS_CHAN_DEFINE(time_channel,			/* Name */
		 struct time_msg,		/* Message type */
		 NULL,				/* Validator */
		 NULL,				/* User data */
		 ZBUS_OBSERVERS_EMPTY,		/* observers */
		 ZBUS_MSG_INIT(.time = { 0 })); /* Initial value */

static struct net_mgmt_event_callback mgmt_cb;
static K_SEM_DEFINE(net_connected, 0, 1);

static void net_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
			      struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&net_connected);
		LOG_INF("Network connected");
	}
}

static void sntp_client_thread(void)
{
	LOG_INF("SNTP client thread started");

	/* Wait for network connection */
	net_mgmt_init_event_callback(&mgmt_cb, net_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);
	LOG_INF("Waiting for network connection...");
	k_sem_take(&net_connected, K_FOREVER);
	net_mgmt_del_event_callback(&mgmt_cb);

	struct sntp_time sntp_time;
	int rv;

	while (1) {
		rv = sntp_simple(CONFIG_SNTP_SERVER, 3000, &sntp_time);
		if (rv == 0) {
			struct tm current_time;
			time_t t = sntp_time.seconds;

			gmtime_r(&t, &current_time);

			/* Set system clock */
			struct timespec ts = { .tv_sec = t, .tv_nsec = 0 };
			clock_settime(CLOCK_REALTIME, &ts);

			struct time_msg msg = { .time = current_time };
			zbus_chan_pub(&time_channel, &msg, K_MSEC(500));
			LOG_INF("SNTP sync successful. Time published to ZBus.");
			char buffer[30];
			strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &current_time);
			LOG_INF("Current time: %s", buffer);

		} else {
			LOG_WRN("SNTP sync failed: %d", rv);
		}
		k_sleep(K_MINUTES(5));
	}
}

static void logger_thread(void)
{
	const struct zbus_channel *chan;
	struct time_msg msg;

	LOG_INF("Logger thread started");

	while (!zbus_sub_wait(&time_sub, &chan, K_FOREVER)) {
		if (&time_channel == chan) {
			zbus_chan_read(&time_channel, &msg, K_MSEC(500));
			char buffer[30];
			strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &msg.time);
			LOG_INF("Logger: Received new time -> %s", buffer);
		}
	}
}

static void application_thread(void)
{
	const struct zbus_channel *chan;
	struct time_msg msg;
	static time_t last_event_time = 0;

	LOG_INF("Application thread started");

	while (!zbus_sub_wait(&app_sub, &chan, K_FOREVER)) {
		if (&time_channel == chan) {
			zbus_chan_read(&time_channel, &msg, K_MSEC(500));
			time_t current_time = mktime(&msg.time);
			if (last_event_time != 0) {
				LOG_INF("Application: Time since last event: %lld seconds",
					current_time - last_event_time);
			} else {
				LOG_INF("Application: First time event received.");
			}
			last_event_time = current_time;
		}
	}
}

/* Thread definitions */
K_THREAD_DEFINE(sntp_client_tid, 2048, sntp_client_thread, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(logger_tid, 1024, logger_thread, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(application_tid, 1024, application_thread, NULL, NULL, NULL, 7, 0, 0);

/* ZBus subscriber definitions */
ZBUS_SUBSCRIBER_DEFINE(time_sub, 4);
ZBUS_SUBSCRIBER_DEFINE(app_sub, 4);

ZBUS_CHAN_ADD_OBS(time_channel, time_sub, app_sub);

void main(void)
{
	/* The threads are started automatically by the kernel. */
}


