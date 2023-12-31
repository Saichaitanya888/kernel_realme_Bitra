cp out/arch/arm64/boot/Image ./anykernel/
cp out/arch/arm64/boot/dtbo.img ./anykernel/


cd anykernel && zip -r pegasus-kernel-AOSP-$(date +"%d-%m-%Y"-%H%M).zip * && mv pegasus-kernel-AOSP-$(date +"%d-%m-%Y-%H%M").zip ../out/

cd ..
