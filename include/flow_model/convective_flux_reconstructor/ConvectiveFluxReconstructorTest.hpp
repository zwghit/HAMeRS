#ifndef CONVECTIVE_FLUX_RECONSTRUCTOR_TEST_HPP
#define CONVECTIVE_FLUX_RECONSTRUCTOR_TEST_HPP

#include "ConvectiveFluxReconstructor.hpp"

#include "Directions.hpp"
#include "flow_model/Riemann_solver/RiemannSolverHLLC_HLL.hpp"

#include "boost/multi_array.hpp"

#define EPSILON 1e-40

class ConvectiveFluxReconstructorTest: public ConvectiveFluxReconstructor
{
    public:
        ConvectiveFluxReconstructorTest(
            const std::string& object_name,
            const tbox::Dimension& dim,
            const boost::shared_ptr<geom::CartesianGridGeometry>& grid_geom,
            const FLOW_MODEL& flow_model,
            const int& num_eqn,
            const int& num_species,
            const boost::shared_ptr<EquationOfState>& equation_of_state,
            const boost::shared_ptr<tbox::Database>& shock_capturing_scheme_db);
        
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
         * of the equtions.
         */
        void
        computeConvectiveFluxAndSource(
            hier::Patch& patch,
            const double time,
            const double dt,
            const boost::shared_ptr<hier::VariableContext> data_context);
    
    private:
        
        /*
         * Constants used by the scheme.
         */
        double d_constant_D;
        double d_constant_r_c;
        double d_constant_delta;
        int d_constant_q;
        
        /*
         * Weights used in WENO interpolations.
         */
        boost::multi_array<double, 2> d_weights_c;
        
        /*
         * Riemann solver used for computing mid-point fluxes.
         */
        RiemannSolverHLLC_HLL d_Riemann_solver;
        
        /*
         * Convert primitive variables into characteristic variables.
         */
        void
        projectPrimitiveVariablesToCharacteristicFields(
            std::vector<double*> characteristic_variables,
            const std::vector<const double*> primitive_variables,
            const boost::multi_array<const double*, 2> projection_matrix);
        
        /*
         * Convert characteristic variables into primitive variables.
         */
        void
        projectCharacteristicVariablesToPhysicalFields(
            std::vector<double*> primitive_variables,
            const std::vector<const double*> characteristic_variables,
            const boost::multi_array<const double*, 2> projection_matrix_inv);
        
        /*
         * Compute sigma's.
         */
        std::vector<double>
        computeSigma(
            const boost::multi_array<double, 2>& W_array);
        
        /*
         * Compute beta's.
         */
        boost::multi_array<double, 2>
        computeBeta(
            const boost::multi_array<double, 2>& W_array);
        
        /*
         * Compute beta_tilde's.
         */
        boost::multi_array<double, 2>
        computeBetaTilde(
            const boost::multi_array<double, 2>& W_array);
        
        /*
         * Perform WENO interpolation.
         */
        void
        performWENOInterpolation(
            std::vector<double>& W_L,
            std::vector<double>& W_R,
            const boost::multi_array<double, 2>& W_array);
};

#endif /* CONVECTIVE_FLUX_RECONSTRUCTOR_TEST_HPP */