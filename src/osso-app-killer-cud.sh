#!/bin/sh
# Script for doing the Cleanup user data (CUD) operation.
#
# This file is part of osso-app-killer.
#
# Copyright (C) 2005-2007 Nokia Corporation. All rights reserved.
#
# Contact: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License 
# version 2 as published by the Free Software Foundation. 
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

DIR=/etc/osso-af-init
DEFHOME=/home/user
export PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/bin/X11'
USER=`whoami`

if test $(id -u) -eq 0; then
  SUDO=''
  HOME=$DEFHOME
  echo "$0: Warning, I'm root"
else
  SUDO='sudo'
  if [ "x$HOME" = "x" ]; then
    HOME=$DEFHOME
    echo "$0: Warning, HOME is not defined, assuming '$HOME'"
  fi
fi

# define MYDOCSDIR etc.
source $DIR/af-defines.sh

# shut down things
if [ -x /etc/init.d/hildon-desktop ]; then
  logger -t CUD 'Stopping hildon-desktop' 
  $SUDO /etc/init.d/hildon-desktop stop
fi
if [ -x /etc/init.d/icd2 ]; then
  logger -t CUD 'Stopping icd2'
  $SUDO /etc/init.d/icd2 stop
fi
logger -t CUD 'Stopping a bunch more things'
$SUDO /etc/init.d/af-base-apps stop
$SUDO $DIR/gconf-daemon.sh stop
if ps ax | grep -v grep | grep -q gconfd-2; then
  $SUDO /usr/bin/killall gconfd-2
fi

if [ "x$OSSO_CUD_DOES_NOT_DESTROY" = "x" ]; then
  # Remove all user data
  logger -t CUD 'Cleaning gconf'
  CUD=foo /usr/sbin/gconf-clean.sh 

  OLDDIR=`pwd`

  logger -t CUD 'Running user cud scripts'

  cd $HOME/.osso-cud-scripts ;# this location should be deprecated
  for f in `ls *.sh`; do
    # if we are root, this is run as root (but no can do because
    # user 'user' might not exist)
    logger -t CUD "$HOME/.osso-cud-scripts/$f"
    ./$f
    RC=$?
    if [ $RC != 0 ]; then
      echo "$0: Warning, '$f' returned non-zero return code $RC"
    fi
  done

  logger -t CUD 'Cleaning mmc'

  /usr/sbin/osso-clean-mmc.sh

  logger -t CUD 'Running system CUD scripts'
  cd /etc/osso-cud-scripts
  for f in `ls *.sh`; do
    # if we are root, this is run as root (but no can do because
    # user 'user' might not exist)
    logger -t CUD "/etc/osso-cud-scripts/$f"
    ./$f
    RC=$?
    if [ $RC != 0 ]; then
      echo "$0: Warning, '$f' returned non-zero return code $RC"
    fi
  done
  cd $OLDDIR
else
  echo "$0: OSSO_CUD_DOES_NOT_DESTROY defined, no data deleted"
fi

logger -t CUD 'Running common bits'

# final cleanup and reboot
CUD=foo source /usr/sbin/osso-app-killer-common.sh

exit 0
