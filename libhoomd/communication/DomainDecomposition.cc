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

// Maintainer: jglaser

/*! \file DomainDecomposition.cc
    \brief Implements the DomainDecomposition class
*/

#ifdef ENABLE_MPI
#include "DomainDecomposition.h"

#include "SystemDefinition.h"
#include "ParticleData.h"

#include <boost/mpi.hpp>
#include <boost/python.hpp>
#include <boost/serialization/map.hpp>

using namespace boost::python;

// Define some of our types as fixed-size MPI datatypes for performance optimization
BOOST_IS_MPI_DATATYPE(Scalar4)
BOOST_IS_MPI_DATATYPE(Scalar3)
BOOST_IS_MPI_DATATYPE(Scalar)
BOOST_IS_MPI_DATATYPE(uint3)
BOOST_IS_MPI_DATATYPE(int3)

//! Constructor
/*! The constructor performs a spatial domain decomposition of the simulation box of processor with rank \b root.
 * The domain dimensions are distributed on the other processors.
 */
DomainDecomposition::DomainDecomposition(boost::shared_ptr<ExecutionConfiguration> exec_conf,
                               Scalar3 L,
                               unsigned int root,
                               unsigned int nx,
                               unsigned int ny,
                               unsigned int nz
                               )
      : m_exec_conf(exec_conf), m_mpi_comm(m_exec_conf->getMPICommunicator())
    {
    unsigned int rank = m_mpi_comm->rank();

    if (rank == root)
        {
        bool found_decomposition = findDecomposition(L, nx, ny, nz);
        if (! found_decomposition)
            {
            m_exec_conf->msg->error() << "***Warning! Unable to find a decomposition of total number of domains == "
                 << m_mpi_comm->size()
                 << endl << "with requested dimensions. Choosing default decomposition.";

            nx = ny = nz = 0;
            findDecomposition(L, nx,ny,nz);
            }
        
        m_nx = nx;
        m_ny = ny;
        m_nz = nz;
       }

    // Print out information about the domain decomposition
    m_exec_conf->msg->notice(1) << "HOOMD-blue is runnning in MPI mode on " << m_mpi_comm->size() << " processors. Decomposition: n_x = " << nx << " n_y = " << ny << " n_z = " << nz << "." << std::endl;
 
    // calculate physical box dimensions of every processor

    // broadcast global box dimensions
    boost::mpi::broadcast(*m_mpi_comm, L, root);

    // broadcast grid dimensions
    boost::mpi::broadcast(*m_mpi_comm, m_nx, root);
    boost::mpi::broadcast(*m_mpi_comm, m_ny, root);
    boost::mpi::broadcast(*m_mpi_comm, m_nz, root);

    // Initialize domain indexer
    m_index = Index3D(m_nx,m_ny,m_nz);

    // calculate position of this box in the domain grid
    m_grid_pos = m_index.getTriple(rank);

    m_root = root;
    }

//! Find a domain decomposition with given parameters
bool DomainDecomposition::findDecomposition(const Scalar3 L, unsigned int& nx, unsigned int& ny, unsigned int& nz)
    {
    assert(L.x > 0);
    assert(L.y > 0);
    assert(L.z > 0);

    // Calulate the number of sub-domains in every direction
    // by minimizing the surface area between domains at constant number of domains
    double min_surface_area = L.x*L.y*m_mpi_comm->size()+L.x*L.z+L.y*L.z;

    unsigned int nx_in = nx;
    unsigned int ny_in = ny;
    unsigned int nz_in = nz;

    bool found_decomposition = (nx_in == 0 && ny_in == 0 && nz_in == 0);

    // initial guess
    nx = 1;
    ny = 1;
    nz = m_mpi_comm->size();


    for (unsigned int nx_try = 1; nx_try <= (unsigned int) m_mpi_comm->size(); nx_try++)
        {
        if (nx_in != 0 && nx_try != nx_in)
            continue;
        for (unsigned int ny_try = 1; nx_try*ny_try <= (unsigned int) m_mpi_comm->size(); ny_try++)
            {
            if (ny_in != 0 && ny_try != ny_in)
                continue;
            for (unsigned int nz_try = 1; nx_try*ny_try*nz_try <= (unsigned int) m_mpi_comm->size(); nz_try++)
                {
                if (nz_in != 0 && nz_try != nz_in)
                    continue;
                if (nx_try*ny_try*nz_try != (unsigned int) m_mpi_comm->size()) continue;
                double surface_area = L.x*L.y*nz_try + L.x*L.z*ny_try + L.y*L.z*nx_try;
                if (surface_area < min_surface_area || !found_decomposition)
                    {
                    nx = nx_try;
                    ny = ny_try;
                    nz = nz_try;
                    min_surface_area = surface_area;
                    found_decomposition = true;
                    }
                }
            }
        }

    return found_decomposition;
    }

//! Calculate MPI ranks of neighboring domain.
unsigned int DomainDecomposition::getNeighborRank(unsigned int dir) const
    {
    assert(0<= dir && dir < 6);

    int adj[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    // determine neighbor position
    int ineigh = (int) m_grid_pos.x + adj[dir][0];
    int jneigh = (int) m_grid_pos.y + adj[dir][1];
    int kneigh = (int) m_grid_pos.z + adj[dir][2];

    // wrap across boundaries
    if (ineigh < 0)
        ineigh += m_nx;
    else if (ineigh == (int) m_nx)
        ineigh -= m_nx;

    if (jneigh < 0)
        jneigh += m_ny;
    else if (jneigh == (int) m_ny)
        jneigh -= m_ny;

    if (kneigh < 0)
        kneigh += m_nz;
    else if (kneigh == (int) m_nz)
        kneigh -= m_nz;

    return m_index(ineigh, jneigh, kneigh);
    }

//! Determines whether the local box shares a boundary with the global box
bool DomainDecomposition::isAtBoundary(unsigned int dir) const
    {
        return ( (dir == 0 && m_grid_pos.x == m_nx - 1) ||
                 (dir == 1 && m_grid_pos.x == 0)        ||
                 (dir == 2 && m_grid_pos.y == m_ny - 1) ||
                 (dir == 3 && m_grid_pos.y == 0)        ||
                 (dir == 4 && m_grid_pos.z == m_nz - 1) ||
                 (dir == 5 && m_grid_pos.z == 0));
    }

//! Get the dimensions of the local simulation box
const BoxDim DomainDecomposition::calculateLocalBox(const BoxDim & global_box)
    {
    // calculate the local box dimensions using domain decomposition information
    Scalar3 L = global_box.getL();
    Scalar3 L_local = L / make_scalar3(m_nx, m_ny, m_nz);

    // position of this domain in the grid
    Scalar3 lo_g = global_box.getLo();
    Scalar3 lo, hi;
    lo.x = lo_g.x + (double)m_grid_pos.x * L_local.x;
    lo.y = lo_g.y + (double)m_grid_pos.y * L_local.y;
    lo.z = lo_g.z + (double)m_grid_pos.z * L_local.z;

    hi = lo + L_local;

    // set periodic flags
    // we are periodic in a direction along which there is only one box
    uchar3 periodic = make_uchar3(m_nx == 1 ? 1 : 0,
                                  m_ny == 1 ? 1 : 0,
                                  m_nz == 1 ? 1 : 0);
 
    return BoxDim(lo, hi, periodic);
    }

//! Export DomainDecomposition class to python
void export_DomainDecomposition()
    {
    class_<DomainDecomposition, boost::shared_ptr<DomainDecomposition>, boost::noncopyable >("DomainDecomposition",
           init< boost::shared_ptr<ExecutionConfiguration>, Scalar3, unsigned int, unsigned int, unsigned int, unsigned int>())
    ;
    }
#endif // ENABLE_MPI
