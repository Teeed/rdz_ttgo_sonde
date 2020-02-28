# Prerequirements

Working docker.

## Build docker image
Just run: 
```
docker build -t ttgo .
```

# Compile project using docker
After building docker image jsut run:
```
docker run --rm -i -t -v `pwd`:/work ttgo /work/build.sh
```
Final binary will be placed in build/out/out.bin.