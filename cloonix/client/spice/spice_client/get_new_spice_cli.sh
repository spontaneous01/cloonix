#!/bin/bash
HERE=`pwd`
rm -rf spice-gtk
rm -f spice-gtk.tar.gz
git clone git://anongit.freedesktop.org/spice/spice-gtk
cd spice-gtk
sed -i s/--enable-gtk-doc/--disable-gtk-doc/ autogen.sh
sed -i s/--enable-vala/--disable-vala/ autogen.sh
cd spice-gtk
./autogen.sh
rm -rf ./autom4te.cache
rm -rf ./spice-common/autom4te.cache
rm -rf .git
rm -rf spice-common/.git
rm -f  gtk-doc.make 
rm -f  m4/gtk-doc.m4 
cd $HERE
tar zcvf spice-gtk.tar.gz spice-gtk
rm -rf spice-gtk

