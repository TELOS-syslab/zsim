#!/bin/bash
# DLRM
cd /mnt/sdb3/data/benchmark/dlrm
sudo apt-get install linux-tools-common linux-tools-generic linux-tools-`uname -r` -y
BASE_DIRECTORY_NAME="dlrm"

rm -rf $BASE_DIRECTORY_NAME
mkdir -p $BASE_DIRECTORY_NAME
cd $BASE_DIRECTORY_NAME
export BASE_PATH=$(pwd)
echo "DLRM-SETUP: FINISHED SETTING UP BASE DIRECTORY"

echo BASE_PATH=$BASE_PATH >> $BASE_PATH/paths.export

wget https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
sudo apt-key add GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
echo "deb https://apt.repos.intel.com/oneapi all main" | \
        sudo tee /etc/apt/sources.list.d/oneAPI.list
sudo apt update || true
sudo apt-get install pkg-config
sudo apt -y install cmake intel-oneapi-vtune numactl python3-pip
sudo sed -i '1i DIAGUTIL_PATH=""' /opt/intel/oneapi/vtune/latest/env/vars.sh
source /opt/intel/oneapi/vtune/latest/env/vars.sh
echo "DLRM-SETUP: FINISHED INSTALLING VTUNE"

cd $BASE_PATH
# mkdir -p miniconda3
# wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh \
#         -O miniconda3/miniconda.sh
# /usr/bin/bash miniconda3/miniconda.sh -b -u -p miniconda3
# rm -rf miniconda3/miniconda.sh
# miniconda3/bin/conda init zsh
# miniconda3/bin/conda init bash
# miniconda3/bin/conda create --name dlrm_cpu python=3.9 ipython -y
conda create --name dlrm_cpu python=3.9 ipython -y
echo "DLRM-SETUP: FINISHED INSTALLING CONDA"
source ~/.bashrc

conda install -n dlrm_cpu astunparse cffi cmake dataclasses future mkl mkl-include ninja \
        pyyaml requests setuptools six typing_extensions -y
conda install -n dlrm_cpu -c conda-forge jemalloc gcc=12.1.0 -y
conda run -n dlrm_cpu pip install git+https://github.com/mlperf/logging
conda run -n dlrm_cpu pip install onnx lark-parser hypothesis tqdm scikit-learn
echo "DLRM-SETUP: FINISHED SETTING UP CONDA ENV"

cd $BASE_PATH
git clone --recursive -b v1.12.1 https://github.com/pytorch/pytorch
cd pytorch
conda run --no-capture-output -n dlrm_cpu pip install -r requirements.txt
export CMAKE_PREFIX_PATH=${CONDA_PREFIX:-"$(dirname $(which conda))/../"}
echo CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH >> $BASE_PATH/paths.export
export TORCH_PATH=$(pwd)
echo TORCH_PATH=$TORCH_PATH >> $BASE_PATH/paths.export
conda run --no-capture-output -n dlrm_cpu python setup.py develop
echo "DLRM-SETUP: FINISHED BUILDLING PYTORCH"

cd $BASE_PATH
git clone --recursive -b v1.12.300 https://github.com/intel/intel-extension-for-pytorch
cd intel-extension-for-pytorch
export IPEX_PATH=$(pwd)
echo IPEX_PATH=$IPEX_PATH >> $BASE_PATH/paths.export
echo "DLRM-SETUP: FINISHED CLONING IPEX"

cd $BASE_PATH
git clone https://github.com/NERSC/itt-python
cd itt-python
git checkout 3fb76911c81cc9ae5ee55101080a58461b99e11c
export VTUNE_PROFILER_DIR=/opt/intel/oneapi/vtune/latest
echo VTUNE_PROFILER_DIR=$VTUNE_PROFILER_DIR >> $BASE_PATH/paths.export
conda run --no-capture-output -n dlrm_cpu python setup.py install --vtune=$VTUNE_PROFILER_DIR
echo "DLRM-SETUP: FINISHED BUILDLING ITT-PYTHON"

# Set up DLRM inference test.
cd $BASE_PATH
git clone https://github.com/rishucoding/reproduce_isca23_cpu_DLRM_inference
cd reproduce_isca23_cpu_DLRM_inference
export DLRM_SYSTEM=$(pwd)
echo DLRM_SYSTEM=$DLRM_SYSTEM >> $BASE_PATH/paths.export
git clone -b pytorch-r1.12-models https://github.com/IntelAI/models.git
cd models
export MODELS_PATH=$(pwd)
echo MODELS_PATH=$MODELS_PATH >> $BASE_PATH/paths.export
mkdir -p models/recommendation/pytorch/dlrm/product

cp $DLRM_SYSTEM/dlrm_patches/dlrm_data_pytorch.py \
    models/recommendation/pytorch/dlrm/product/dlrm_data_pytorch.py
cp $DLRM_SYSTEM/dlrm_patches/dlrm_s_pytorch.py \
    models/recommendation/pytorch/dlrm/product/dlrm_s_pytorch.py
echo "DLRM-SETUP: FINISHED SETTING UP DLRM TEST"

cd $IPEX_PATH
git apply $DLRM_SYSTEM/dlrm_patches/ipex.patch
find . -type f -exec sed -i 's/-Werror//g' {} \;
USE_NATIVE_ARCH=1 CXXFLAGS="-D_GLIBCXX_USE_CXX11_ABI=0" conda run --no-capture-output -n dlrm_cpu python setup.py install
echo "DLRM-SETUP: FINISHED BUILDING IPEX"