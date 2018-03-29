/* 
 * xt_ndpi.h
 * Copyright (C) 2010-2012 G. Elian Gidoni <geg@gnu.org>
 *               2012 Ed Wildgoose <lists@wildgooses.com>
 *               2014 Humberto Jucá <betolj@gmail.com>
 * 
 * This file is part of nDPI, an open source deep packet inspection
 * library based on the PACE technology by ipoque GmbH
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _LINUX_NETFILTER_XT_NDPI_H
#define _LINUX_NETFILTER_XT_NDPI_H 1

#include <linux/netfilter.h>
#include "ndpi_main.h"

#ifndef NDPI_BITMASK_IS_ZERO
#define NDPI_BITMASK_IS_ZERO(a) NDPI_BITMASK_IS_EMPTY(a)
#endif

struct xt_ndpi_mtinfo {
        NDPI_PROTOCOL_BITMASK flags;
};

/* /usr/src/nDPI/src/include/ndpi_protocol_ids.h */
#ifndef NDPI_PROTOCOL_LONG_STRING
#define NDPI_PROTOCOL_LONG_STRING "Unknown","FTP_CONTROL","MAIL_POP","MAIL_SMTP","MAIL_IMAP","DNS","IPP","HTTP","MDNS","NTP",\
"NETBIOS","NFS","SSDP","BGP","SNMP","XDMCP","SMB","SYSLOG","DHCP","POSTGRES","MYSQL","TDS","DIRECT_DOWNLOAD_LINK","MAIL_POPS",\
"APPLEJUICE","DIRECTCONNECT","SOCRATES","WINMX","VMWARE","MAIL_SMTPS",\
"FILETOPIA","IMESH","KONTIKI","OPENFT","FASTTRACK","GNUTELLA","EDONKEY","BITTORRENT",\
"EPP","AVI","FLASH","OGG","MPEG","QUICKTIME","REALMEDIA","WINDOWSMEDIA","MMS","XBOX","QQ","MOVE","RTSP","MAIL_IMAPS","ICECAST",\
"PPLIVE","PPSTREAM","ZATTOO","SHOUTCAST","SOPCAST","TVANTS","TVUPLAYER","HTTP_APPLICATION_VEOHTV","QQLIVE","THUNDER","SOULSEEK",\
"SSL_NO_CERT","IRC","AYIYA","UNENCRYPED_JABBER","MSN","OSCAR","YAHOO","BATTLEFIELD","QUAKE","VRRP","STEAM","HALFLIFE2",\
"WORLDOFWARCRAFT","TELNET","STUN","IPSEC","GRE","ICMP","IGMP","EGP","SCTP","OSPF","IP_IN_IP","RTP","RDP","VNC","PCANYWHERE",\
"SSL","SSH","USENET","MGCP","IAX","TFTP","AFP","STEALTHNET","AIMINI","SIP","TRUPHONE","ICMPV6","DHCPV6","ARMAGETRON",\
"CROSSFIRE","DOFUS","FIESTA","FLORENSIA","GUILDWARS","HTTP_APPLICATION_ACTIVESYNC","KERBEROS","LDAP","MAPLESTORY","MSSQL",\
"PPTP","WARCRAFT3","WORLD_OF_KUNG_FU","MEEBO","FACEBOOK","TWITTER","DROPBOX","GMAIL","GOOGLE_MAPS","YOUTUBE","SKYPE","GOOGLE",\
"DCERPC","NETFLOW","SFLOW","HTTP_CONNECT","HTTP_PROXY","CITRIX","NETFLIX","LASTFM","GROOVESHARK",\
"SKYFILE_PREPAID","SKYFILE_RUDICS","SKYFILE_POSTPAID","CITRIX_ONLINE","APPLE","WEBEX","WHATSAPP","APPLE_ICLOUD","VIBER",\
"APPLE_ITUNES","RADIUS","WINDOWS_UPDATE","TEAMVIEWER","TUENTI","LOTUS_NOTES","SAP","GTP","UPNP","LLMNR","REMOTE_SCAN","SPOTIFY",\
"WEBM","H323","OPENVPN","NOE","CISCOVPN","TEAMSPEAK","TOR","SKINNY","RTCP","RSYNC","ORACLE","CORBA","UBUNTUONE","WHOIS_DAS",\
"COLLECTD","SOCKS5","SOCKS4","RTMP","FTP_DATA","WIKIPEDIA","ZMQ","AMAZON","EBAY","CNN","MEGACO","REDIS","PANDO","VHUA","TELEGRAM",\
"VEVO","PANDORA","QUIC","WHATSAPP_VOICE","CHECK"
#endif

#ifndef NDPI_PROTOCOL_SHORT_STRING
#define NDPI_PROTOCOL_SHORT_STRING "unknown","ftp","pop","smtp","imap","dns","ipp","http","mdns","ntp",\
"netbios","nfs","ssdp","bgp","snmp","xdmcp","smb","syslog","dhcp","postgres","mysql","tds","directdownload","pops",\
"applejuice","directconnect","socrates","winmx","vmware","smtps",\
"filetopia","imesh","kontiki","openft","fasttrack","gnutella","edonkey","bittorrent",\
"epp","avi","flash","ogg","mpeg","quicktime","realmedia","windowsmedia","mms","xbox","qq","move","rtsp","imaps","icecast",\
"pplive","ppstream","zattoo","shoutcast","sopcast","tvants","tvuplayer","http_application_veohtv","qqlive","thunder","soulseek",\
"ssl_no_cert","irc","ayiya","unencryped_jabber","msn","oscar","yahoo","battlefield","quake","vrrp","steam","halflife2",\
"worldofwarcraft","telnet","stun","ipsec","gre","icmp","igmp","egp","sctp","ospf","ipip","rtp","rdp","vnc","pcanywhere",\
"ssl","ssh","usenet","mgcp","iax","tftp","afp","stealthnet","aimini","sip","truphone","icmpv6","dhcpv6","armagetron",\
"crossfire","dofus","fiesta","florensia","guildwars","http_application_activesync","kerberos","ldap","maplestory","mssql",\
"pptp","warcraft3","world_of_kung_fu","meebo","facebook","twitter","dropbox","gmail","google_maps","youtube","skype","google",\
"dcerpc","netflow","sflow","http_connect","http_proxy","citrix","netflix","lastfm","grooveshark",\
"skyfile_prepaid","skyfile_rudics","skyfile_postpaid","citrix_online","apple","webex","whatsapp","apple_icloud","viber",\
"apple_itunes","radius","windows_update","teamviewer","tuenti","lotusnotes","sap","gtp","upnp","llmnr","remotescan","spotify",\
"webm","h323","openvpn","noe","ciscovpn","teamspeak","tor","skinny","rtcp","rsync","oracle","corba","ubuntuone","whois_das",\
"collectd","socks5","socks4","rtmp","ftpdata","wikipedia","zmq","amazon","ebay","cnn","megaco","redis","pando","vhua","telegram",\
"vevo","pandora","quic","whatsapp_voice","dpi_check"
#endif

#ifndef NDPI_LAST_NFPROTO
#define NDPI_LAST_NFPROTO NDPI_LAST_IMPLEMENTED_PROTOCOL + 1
#endif

#endif /* _LINUX_NETFILTER_XT_NDPI_H */
