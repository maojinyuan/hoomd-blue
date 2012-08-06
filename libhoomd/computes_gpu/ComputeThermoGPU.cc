/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2008-2011 Ames Laboratory
Iowa State University and The Regents of the University of Michigan All rights
reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

You may redistribute, use, and create derivate works of HOOMD-blue, in source
and binary forms, provided you abide by the following conditions:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer both in the code and
prominently in any materials provided with the distribution.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* All publications and presentations based on HOOMD-blue, including any reports
or published results obtained, in whole or in part, with HOOMD-blue, will
acknowledge its use according to the terms posted at the time of submission on:
http://codeblue.umich.edu/hoomd-blue/citations.html

* Any electronic documents citing HOOMD-Blue will link to the HOOMD-Blue website:
http://codeblue.umich.edu/hoomd-blue/

* Apart from the above required attributions, neither the name of the copyright
holder nor the names of HOOMD-blue's contributors may be used to endorse or
promote products derived from this software without specific prior written
permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR ANY
WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Maintainer: joaander

/*! \file ComputeThermoGPU.cc
    \brief Contains code for the ComputeThermoGPU class
*/

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4103 4244 )
#endif

#include "ComputeThermoGPU.h"
#include "ComputeThermoGPU.cuh"

#include <boost/python.hpp>
using namespace boost::python;
#include <boost/bind.hpp>
using namespace boost;

#ifdef ENABLE_MPI
#include "Communicator.h"
#endif

#include <iostream>
using namespace std;

/*! \param sysdef System for which to compute thermodynamic properties
    \param group Subset of the system over which properties are calculated
    \param suffix Suffix to append to all logged quantity names
*/

ComputeThermoGPU::ComputeThermoGPU(boost::shared_ptr<SystemDefinition> sysdef,
                                   boost::shared_ptr<ParticleGroup> group,
                                   const std::string& suffix)
    : ComputeThermo(sysdef, group, suffix)
    {
    if (!exec_conf->isCUDAEnabled())
        {
        m_exec_conf->msg->error() << "Creating a ComputeThermoGPU with no GPU in the execution configuration" << endl;
        throw std::runtime_error("Error initializing ComputeThermoGPU");
        }

    m_block_size = 512;
    m_num_blocks = m_group->getNumMembers() / m_block_size + 1;
    
    GPUArray< float4 > scratch(m_num_blocks, exec_conf);
    m_scratch.swap(scratch);

    GPUArray< float > scratch_pressure_tensor(m_num_blocks * 6, exec_conf);
    m_scratch_pressure_tensor.swap(scratch_pressure_tensor);
    }



/*! Computes all thermodynamic properties of the system in one fell swoop, on the GPU.
 */
void ComputeThermoGPU::computeProperties()
    {
    unsigned int group_size = m_group->getNumLocalMembers();
    // just drop out if the group is an empty group
    if (group_size == 0)
        return;
    
    if (m_prof) m_prof->push("Thermo");
    
    assert(m_pdata);
    assert(m_ndof != 0);
    
    // access the particle data
    ArrayHandle<Scalar4> d_vel(m_pdata->getVelocities(), access_location::device, access_mode::read);
    BoxDim box = m_pdata->getGlobalBox();
    
    PDataFlags flags = m_pdata->getFlags();

    { // scope these array handles so they are released before the additional terms are added
    // access the net force, pe, and virial
    const GPUArray< Scalar4 >& net_force = m_pdata->getNetForce();
    const GPUArray< Scalar >& net_virial = m_pdata->getNetVirial();
    ArrayHandle<Scalar4> d_net_force(net_force, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_net_virial(net_virial, access_location::device, access_mode::read);
    ArrayHandle<float4> d_scratch(m_scratch, access_location::device, access_mode::overwrite);
    ArrayHandle<float> d_scratch_pressure_tensor(m_scratch_pressure_tensor, access_location::device, access_mode::overwrite);
    ArrayHandle<float> d_properties(m_properties, access_location::device, access_mode::overwrite);
    
    // access the group
    ArrayHandle< unsigned int > d_index_array(m_group->getIndexArray(), access_location::device, access_mode::read);
    
    // build up args list
    m_num_blocks = m_group->getNumLocalMembers() / m_block_size + 1;
    compute_thermo_args args;
    args.d_net_force = d_net_force.data;
    args.d_net_virial = d_net_virial.data;
    args.virial_pitch = net_virial.getPitch();
    args.ndof = m_ndof;
    args.D = m_sysdef->getNDimensions();
    args.d_scratch = d_scratch.data;
    args.d_scratch_pressure_tensor = d_scratch_pressure_tensor.data;
    args.block_size = m_block_size;
    args.n_blocks = m_num_blocks;

    // perform the computation on the GPU
    gpu_compute_thermo( d_properties.data,
                        d_vel.data,
                        d_index_array.data,
                        group_size,
                        box,
                        args,
                        flags[pdata_flag::pressure_tensor]);
   
    if (exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();
    }

#ifdef ENABLE_MPI
    // for MPI, we have to copy data back to the host to perform collective operations

    boost::shared_ptr<const boost::mpi::communicator> mpi_comm;
    if (m_comm)
        {
        mpi_comm = m_exec_conf->getMPICommunicator();
        assert(mpi_comm);
        }

    if (mpi_comm)
        {
        ArrayHandle<Scalar> h_properties(m_properties, access_location::host, access_mode::readwrite);

        if (m_prof)
            m_prof->push("MPI collectives");

        Scalar & T = h_properties.data[thermo_index::temperature];
        Scalar & P = h_properties.data[thermo_index::pressure];
        Scalar & ke =  h_properties.data[thermo_index::kinetic_energy];
        Scalar & pe =  h_properties.data[thermo_index::potential_energy];
        Scalar & Pxx =  h_properties.data[thermo_index::pressure_xx];
        Scalar & Pxy =  h_properties.data[thermo_index::pressure_xy];
        Scalar & Pxz =  h_properties.data[thermo_index::pressure_xz];
        Scalar & Pyy =  h_properties.data[thermo_index::pressure_yy];
        Scalar & Pyz =  h_properties.data[thermo_index::pressure_yz];
        Scalar & Pzz =  h_properties.data[thermo_index::pressure_zz];
        T = all_reduce(*mpi_comm, T, std::plus<Scalar>());

        if (flags[pdata_flag::isotropic_virial])
            P = all_reduce(*mpi_comm, P, std::plus<Scalar>());

        ke = all_reduce(*mpi_comm, ke, std::plus<Scalar>());

        if (flags[pdata_flag::potential_energy])
            pe = all_reduce(*mpi_comm, pe, std::plus<Scalar>());

        if (flags[pdata_flag::pressure_tensor])
            {
            Pxx = all_reduce(*mpi_comm, Pxx, std::plus<Scalar>());
            Pxy = all_reduce(*mpi_comm, Pxy, std::plus<Scalar>());
            Pxz = all_reduce(*mpi_comm, Pxz, std::plus<Scalar>());
            Pyy = all_reduce(*mpi_comm, Pyy, std::plus<Scalar>());
            Pyz = all_reduce(*mpi_comm, Pyz, std::plus<Scalar>());
            Pzz = all_reduce(*mpi_comm, Pzz, std::plus<Scalar>());
            }
        if (m_prof)
            m_prof->pop();
        }
#endif

    if (m_prof) m_prof->pop();
    }

void export_ComputeThermoGPU()
    {
    class_<ComputeThermoGPU, boost::shared_ptr<ComputeThermoGPU>, bases<ComputeThermo>, boost::noncopyable >
        ("ComputeThermoGPU", init< boost::shared_ptr<SystemDefinition>,
         boost::shared_ptr<ParticleGroup>,
         const std::string& >())
        ;
    }

#ifdef WIN32
#pragma warning( pop )
#endif

