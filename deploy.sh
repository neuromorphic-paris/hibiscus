host='nvidia@20.20.20.99'
path='hibiscus'

for file in source/*.hpp; do clang-format -i $file; done;
for file in source/*.cpp; do clang-format -i $file; done;
for file in third_party/hummingbird/source/*.hpp; do clang-format -i $file; done;
for file in third_party/hummingbird/source/*.cpp; do clang-format -i $file; done;
clang-format -i teensy/eventide/eventide.ino
clang-format -i teensy/record/record.ino

ssh root@20.20.20.99 "date -u -s '$(date -u)'"
rsync -avz --exclude build ./ $host:$path/
ssh $host "cd $path && premake4 gmake && cd build && make"
ssh $host "cd $path/third_party/hummingbird && premake4 gmake && cd build && make"
