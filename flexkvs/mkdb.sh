rm output.dat
touch output.dat
sudo chmod 777 output.dat
dd if=/dev/zero of=output.dat bs=1G seek=100 count=0

