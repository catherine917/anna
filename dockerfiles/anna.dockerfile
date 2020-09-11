#  Copyright 2018 U.C. Berkeley RISE Lab
# 
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
# 
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

FROM hydroproject/base:latest

ARG repo_org=catherine917
ARG source_branch=benchmark
ARG build_branch=benchmark

USER root

# Check out to the appropriate branch on the appropriate fork of the repository
# and build Anna.
WORKDIR $HYDRO_HOME/anna
RUN git remote remove origin && git remote add origin https://github.com/$repo_org/anna
RUN git fetch origin && git checkout -b $build_branch origin/$source_branch
RUN git submodule init && git submodule update && cd common && git pull origin master
RUN bash scripts/build.sh -j4 -bRelease
WORKDIR /

COPY start-anna.sh /
CMD bash start-anna.sh $SERVER_TYPE
