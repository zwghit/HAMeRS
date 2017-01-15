#ifndef CONVECTIVE_FLUX_RECONSTRUCTOR_WCNS6_TEST_HPP
#define CONVECTIVE_FLUX_RECONSTRUCTOR_WCNS6_TEST_HPP

#include "SAMRAI/pdat/SideVariable.h"

#include "flow/convective_flux_reconstructors/ConvectiveFluxReconstructor.hpp"
#include "util/Directions.hpp"

#include "boost/multi_array.hpp"

class ConvectiveFluxReconstructorWCNS6_Test: public ConvectiveFluxReconstructor
{
    public:
        ConvectiveFluxReconstructorWCNS6_Test(
            const std::string& object_name,
            const tbox::Dimension& dim,
            const boost::shared_ptr<geom::CartesianGridGeometry>& grid_geometry,
            const int& num_eqn,
            const int& num_species,
            const boost::shared_ptr<FlowModel>& flow_model,
            const boost::shared_ptr<tbox::Database>& convective_flux_reconstructor_db);
        
        ~ConvectiveFluxReconstructorWCNS6_Test();
        
        /*
         * Print all characteristics of the convective flux reconstruction class.
         */
        void
        printClassData(std::ostream& os) const;
        
        /*
         * Put the characteristics of the convective flux reconstruction class
         * into the restart database.
         */
        void
        putToRestart(
            const boost::shared_ptr<tbox::Database>& restart_db) const;
        
        /*
         * Compute the convective fluxes and sources due to hyperbolization
         * of the equations.
         */
        void
        computeConvectiveFluxesAndSources(
            hier::Patch& patch,
            const double time,
            const double dt,
            const int RK_step_number,
            const boost::shared_ptr<pdat::FaceVariable<double> >& variable_convective_flux,
            const boost::shared_ptr<pdat::CellVariable<double> >& variable_source,
            const boost::shared_ptr<hier::VariableContext>& data_context);
        
    private:
        /*
         * Perform WENO interpolation.
         */
        void
        performWENOInterpolation(
            std::vector<boost::shared_ptr<pdat::SideData<double> > >& variables_minus,
            std::vector<boost::shared_ptr<pdat::SideData<double> > >& variables_plus,
            const std::vector<std::vector<boost::shared_ptr<pdat::SideData<double> > > >& variables);
        
        /*
         * Constants used by the scheme.
         */
        int    d_constant_p;
        int    d_constant_q;
        double d_constant_C;
        double d_constant_alpha_tau;
        
        /*
         * Timers interspersed throughout the class.
         */
        
        static boost::shared_ptr<tbox::Timer> t_characteristic_decomposition;
        static boost::shared_ptr<tbox::Timer> t_WENO_interpolation;
        static boost::shared_ptr<tbox::Timer> t_Riemann_solver;
        static boost::shared_ptr<tbox::Timer> t_reconstruct_flux;
        static boost::shared_ptr<tbox::Timer> t_compute_source;
        
};

#endif /* CONVECTIVE_FLUX_RECONSTRUCTOR_WCNS6_TEST_HPP */
