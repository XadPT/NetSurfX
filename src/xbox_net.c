/*
 * NetSurfX - arranque da rede em segundo plano.
 *
 * Le a configuracao do dashboard (IP automatico/manual e DNS manual, como o
 * DNS do Insignia) e inicia o lwIP numa thread, para o browser aparecer logo.
 * O DHCP da Xbox pode demorar mais de 10 s (vimos RC -2 na consola), por isso
 * continuamos a esperar pelo endereco e so entao aplicamos o DNS manual.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <nxdk/net.h>
#include <nxdk/configsector.h>
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#include <lwip/dns.h>

#include "xbox_net.h"

static volatile int g_state;     /* 0 a arrancar, 1 com IP, -1 sem rede */
static volatile int g_rc;
static char g_ip[24] = "-";
static char g_dns[24] = "-";
static HANDLE g_thread;

static void ip_str(uint32_t v, char *out, size_t n)
{
	snprintf(out, n, "%u.%u.%u.%u", (unsigned)(v & 255), (unsigned)((v >> 8) & 255),
		 (unsigned)((v >> 16) & 255), (unsigned)((v >> 24) & 255));
}

static void set_dns(int idx, uint32_t v)
{
	ip_addr_t a;
	IP_ADDR4(&a, v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255);
	dns_setserver((u8_t)idx, &a);
}

static DWORD WINAPI net_thread(LPVOID arg)
{
	nxdk_network_config_sector_t cfg;
	nx_net_parameters_t np;
	bool have_cfg;
	(void)arg;

	memset(&cfg, 0, sizeof cfg);
	have_cfg = nxLoadNetworkConfig(&cfg);
	memset(&np, 0, sizeof np);
	np.ipv4_mode = NX_NET_DHCP;
	np.ipv6_mode = NX_NET_DHCP;
	if (have_cfg && (cfg.dhcpFlags & NXDK_NETWORK_CONFIG_MANUAL_IP)) {
		np.ipv4_mode = NX_NET_STATIC;
		np.ipv4_ip = cfg.manual.ip;
		np.ipv4_netmask = cfg.manual.subnetMask;
		np.ipv4_gateway = cfg.manual.defaultGateway;
	}
	if (have_cfg && (cfg.dhcpFlags & NXDK_NETWORK_CONFIG_MANUAL_DNS)) {
		np.ipv4_dns1 = cfg.manual.primaryDns;
		np.ipv4_dns2 = cfg.manual.secondaryDns;
	}
	g_rc = nxNetInit(&np);
	if (g_rc == -1) {           /* nem sequer arrancou (sem placa/cabo?) */
		g_state = -1;
		return 0;
	}

	/* espera pelo endereco (o DHCP pode chegar depois do nxNetInit desistir) */
	for (int i = 0; ; i++) {
		if (netif_default && !ip4_addr_isany_val(*netif_ip4_addr(netif_default))) {
			ip_str(ip4_addr_get_u32(netif_ip4_addr(netif_default)), g_ip, sizeof g_ip);
			if (have_cfg && (cfg.dhcpFlags & NXDK_NETWORK_CONFIG_MANUAL_DNS)) {
				if (cfg.manual.primaryDns) set_dns(0, cfg.manual.primaryDns);
				if (cfg.manual.secondaryDns) set_dns(1, cfg.manual.secondaryDns);
			} else {
				const ip_addr_t *d = dns_getserver(0);
				if (d == NULL || ip4_addr_isany_val(*ip_2_ip4(d)))
					set_dns(0, 0x01010101);   /* 1.1.1.1 de recurso */
			}
			{
				const ip_addr_t *d = dns_getserver(0);
				if (d) ip_str(ip4_addr_get_u32(ip_2_ip4(d)), g_dns, sizeof g_dns);
			}
			g_state = 1;
			return 0;
		}
		if (i == 120) g_state = -1;   /* 60 s sem IP: avisa, mas continua a tentar */
		Sleep(500);
	}
}

void nsx_net_start(void)
{
	if (g_thread == NULL)
		g_thread = CreateThread(NULL, 64 * 1024, net_thread, NULL, 0, NULL);
}

int nsx_net_wait(int ms)
{
	int waited = 0;
	while (g_state != 1) {
		if (waited >= ms) return 0;
		Sleep(100);
		waited += 100;
	}
	return 1;
}

int nsx_net_state(void) { return g_state; }

void nsx_net_describe(char *buf, size_t n)
{
	switch (g_state) {
	case 1:  snprintf(buf, n, "Rede: IP %s, DNS %s", g_ip, g_dns); break;
	case -1: snprintf(buf, n, "Rede: sem endereco (cabo ligado? router com DHCP?)"); break;
	default: snprintf(buf, n, "Rede: a ligar..."); break;
	}
}
