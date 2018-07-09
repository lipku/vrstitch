#!/usr/bin/env bash

set -e

SCRIPT=`pwd`/$0
FILENAME=`basename $SCRIPT`
PATHNAME=`dirname $SCRIPT`
ROOT=$PATHNAME/..
BUILD_DIR=$ROOT/build
CURRENT_DIR=`pwd`

LIB_DIR=$BUILD_DIR/libdeps
PREFIX_DIR=$LIB_DIR/build/
FAST_MAKE=''


parse_arguments(){
  while [ "$1" != "" ]; do
    case $1 in
      "--enable-gpl")
        ENABLE_GPL=true
        ;;
      "--cleanup")
        CLEANUP=true
        ;;
      "--fast")
        FAST_MAKE='-j4'
        ;;
    esac
    shift
  done
}


install_apt_deps(){
 # sudo apt-get update -y
  sudo apt-get install -qq git make cmake unzip -y
}


install_live555(){
    unzip live.zip
    cd live
    ./genMakefiles linux-64bit
    make $FAST_MAKE -s V=0
    sudo make install

    cd $CURRENT_DIR
}

install_fdkaac(){
    tar -zxvf fdk-aac-0.1.5.tar.gz
    cd fdk-aac-0.1.5
    ./configure
    make $FAST_MAKE -s V=0
    sudo make install

    cd $CURRENT_DIR
}

install_portaudio(){
    sudo apt-get install -qq libasound-dev
    tar -zxvf pa_stable_v190600_20161030.tgz
    cd portaudio
    ./configure
    make $FAST_MAKE -s V=0
    sudo make install

    cd $CURRENT_DIR
}


install_ffmpeg(){
  sudo apt-get -qq install yasm libx264.
  tar -jxvf ffmpeg-4.0.tar.bz2
  cd ffmpeg-4.0
  ./configure --enable-shared --enable-gpl --enable-nonfree --enable-libx264 --disable-doc
  make $FAST_MAKE -s V=0
  sudo make install

  cd $CURRENT_DIR
}


install_srs(){
    unzip srs-2.0-r2.zip
    cd srs-2.0-r2/trunk
    ./configure
    make $FAST_MAKE -s V=0

    cd $CURRENT_DIR
}


install_nvstitch(){
    tar -xzvf VRWorks_360_Video_1_5_Linux_SDK.tar.gz
    cd VRWorks_360_Video_SDK_1.5_Linux_package/samples
    mv nvstitch_sample nvstitch_sample-orig
    git clone https://github.com/lipku/vrstitch.git nvstitch_sample
    cd ..
    mkdir build
    cd build
    cmake ..
    make
    cp ../samples/nvstitch_sample/NV12ToARGB_drvapi_x64.ptx ./
    cp ../samples/nvstitch_sample/config.xml ./
    cp ../samples/nvstitch_sample/run.sh ./
    cp $CURRENT_DIR/footage/* ../footage/

    cd $CURRENT_DIR
}


cleanup(){
  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -r libnice*
    rm -r libsrtp*
    rm -r libav*
    rm -r v11*
    rm -r openssl*
    rm -r opus*
    cd $CURRENT_DIR
  fi
}

parse_arguments $*

install_apt_deps
install_live555
install_fdkaac
install_ffmpeg
install_portaudio
install_srs
install_nvstitch

sudo cp rc.local /etc/rc.local

echo "cd srs-2.0-r2/trunk"
echo "./objs/srs -c conf/srs.conf"
echo "cd VRWorks_360_Video_SDK_1.5_Linux_package/build"
echo "./run.sh"


if [ "$CLEANUP" = "true" ]; then
  echo "Cleaning up..."
  cleanup
fi
