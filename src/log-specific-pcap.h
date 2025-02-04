/* Copyright (C) 2007-2014 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */


/**
 * \file
 *
 * \author William Metcalf <William.Metcalf@gmail.com>
 * \author Victor Julien <victor@inliniac.net>
 *
 * Specific pcap packet logging module.
 */

#ifndef __SPECIFIC_LOG_PCAP_H__
#define __SPECIFIC_LOG_PCAP_H__

#define DEFAULT_LOG_SSP_PATH_LEN (128)
#define DEFAULT_LOG_SSP_PATH "log-specific-pcap"

void SPPcapLogRegister(void);
void SPPcapLogProfileSetup(void);
void SPPcapLogCloseFileCtx(void *fptr);

#endif /* __LOG_PCAP_H__ */

