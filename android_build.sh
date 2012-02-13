 NDKROOT="/home/indal.choi/bin/android-ndk-r7" \
 AR="$NDKROOT/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86/bin/arm-linux-androideabi-ar" \
 LD="$NDKROOT/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86/bin/arm-linux-androideabi-ld" \
 CC="$NDKROOT/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86/bin/arm-linux-androideabi-gcc" \
 CPPFLAGS="-g --sysroot=/home/indal.choi/bin/android-ndk-r7/platforms/android-9/arch-arm -fvisibility=default -D__WORDSIZE=32" \
 CFLAGS="-g --sysroot=/home/indal.choi/bin/android-ndk-r7/platforms/android-9/arch-arm -fvisibility=default -D__WORDSIZE=32" \
 AM_CFLAGS=""\
   ./configure --prefix=/system/bin \
   --host=armv7-unknown-linux --disable-werror 
#   --with-libelf="/home/indal.choi/bin/android-ndk-r7/platforms/android-9/arch-arm/usr" --disable-werror
#   --with-sysroot="$NDKROOT/platforms/android-9/arch-arm" \
