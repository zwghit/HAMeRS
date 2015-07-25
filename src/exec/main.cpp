#include "SAMRAI/SAMRAI_config.h"

// Headers for basic SAMRAI objects

#include "SAMRAI/hier/BoxContainer.h"
#include "SAMRAI/hier/Index.h"
#include "SAMRAI/hier/PatchLevel.h"
#include "SAMRAI/hier/VariableDatabase.h"
#include "SAMRAI/tbox/BalancedDepthFirstTree.h"
#include "SAMRAI/tbox/Database.h"
#include "SAMRAI/tbox/InputDatabase.h"
#include "SAMRAI/tbox/InputManager.h"
#include "SAMRAI/tbox/PIO.h"
#include "SAMRAI/tbox/RestartManager.h"
#include "SAMRAI/tbox/SAMRAI_MPI.h"
#include "SAMRAI/tbox/SAMRAIManager.h"
#include "SAMRAI/tbox/SiloDatabaseFactory.h"
#include "SAMRAI/tbox/Timer.h"
#include "SAMRAI/tbox/TimerManager.h"
#include "SAMRAI/tbox/Utilities.h"

// Headers for major algorithm/data structure objects

#include "SAMRAI/algs/TimeRefinementIntegrator.h"
#include "SAMRAI/algs/TimeRefinementLevelStrategy.h"
#include "SAMRAI/appu/VisItDataWriter.h"
#include "SAMRAI/geom/CartesianGridGeometry.h"
#include "SAMRAI/hier/PatchHierarchy.h"
#include "SAMRAI/mesh/BergerRigoutsos.h"
#include "SAMRAI/mesh/GriddingAlgorithm.h"
#include "SAMRAI/mesh/StandardTagAndInitialize.h"
#include "SAMRAI/mesh/TreeLoadBalancer.h"

// Headers for application-specific algorithm/data structure object

#include "applications/Euler/Euler.hpp"
#include "integrator/RungeKuttaLevelIntegrator.hpp"

#include "boost/shared_ptr.hpp"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <sys/stat.h>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace SAMRAI;

/*
 ************************************************************************
 *
 * This is the main program for an AMR Euler gas dynamics application
 * built using SAMRAI.   The application program is constructed by
 * composing a variety of algorithm objects found in SAMRAI plus some
 * others that are specific to this application.   The following brief
 * discussion summarizes these objects.
 *
 *    hier::PatchHierarchy - A container for the AMR patch hierarchy and
 *       the data on the grid.
 *
 *    geom::CartesianGridGeometry - Defines and maintains the Cartesian
 *       coordinate system on the grid.  The hier::PatchHierarchy
 *       maintains a reference to this object.
 *
 * A single overarching algorithm object drives the time integration
 * and adaptive gridding processes:
 *
 *    algs::TimeRefinementIntegrator - Coordinates time integration and
 *       adaptive gridding procedures for the various levels
 *       in the AMR patch hierarchy.  Local time refinement is
 *       employed during hierarchy integration; i.e., finer
 *       levels are advanced using smaller time increments than
 *       coarser level.  Thus, this object also invokes data
 *       synchronization procedures which couple the solution on
 *       different patch hierarchy levels.
 *
 * The time refinement integrator is not specific to the numerical
 * methods used and the problem being solved.   It maintains references
 * to two other finer grain algorithmic objects, more specific to
 * the problem at hand, with which it is configured when they are
 * passed into its constructor.   They are:
 *
 *    RungeKuttaLevelIntegrator - Defines data management procedures
 *       for level integration, data synchronization between levels,
 *       and tagging cells for refinement.  These operations are
 *       tailored to explicit Runge-Kutta time integration algorithms
 *       used for hyperbolic systems of conservation laws, such as
 *       the Euler equations.  This integrator manages data for
 *       numerical routines that treat individual patches in the AMR
 *       patch hierarchy.  In this particular application, it maintains
 *       a pointer to the Euler object that defines variables and
 *       provides numerical routines for the Euler model.
 *
 *       Euler - Defines variables and numerical routines for the
 *          discrete Euler equations on each patch in the AMR
 *          hierarchy.
 *
 *    mesh::GriddingAlgorithm - Drives the AMR patch hierarchy generation
 *       and regridding procedures.  This object maintains
 *       references to three other algorithmic objects with
 *       which it is configured when they are passed into its
 *       constructor.   They are:
 *
 *       mesh::BergerRigoutsos - Clusters cells tagged for refinement on a
 *          patch level into a collection of logically-rectangular
 *          box domains.
 *
 *       mesh::TreeLoadBalancer - Processes the boxes generated by the
 *          mesh::BergerRigoutsos algorithm into a configuration from
 *          which patches are contructed.  The algorithm we use in this
 *          class assumes a spatially-uniform workload distribution;
 *          thus, it attempts to produce a collection of boxes
 *          each of which contains the same number of cells.  The
 *          load balancer also assigns patches to processors.
 *
 *       mesh::StandardTagAndInitialize - Couples the gridding algorithm
 *          to the RungeKuttaIntegrator. Selects cells for
 *          refinement based on either Gradient detection, Richardson
 *          extrapolation, or pre-defined Refine box region.  The
 *          object maintains a pointer to the RungeKuttaLevelIntegrator,
 *          which is passed into its constructor, for this purpose.
 *
 ************************************************************************
 */

/*
 *******************************************************************
 *
 * For each run, the input filename and restart information
 * (if needed) must be given on the command line.
 *
 *      For non-restarted case, command line is:
 *
 *          executable <input file name>
 *
 *      For restarted run, command line is:
 *
 *          executable <input file name> <restart directory> \
 *                     <restart number>
 *
 * Accessory routines used within the main program:
 *
 *   dumpVizData1dPencil - Writes 1d pencil of Euler solution data
 *      to plot files so that it may be viewed in MatLab.  This
 *      routine assumes a single patch level in 2d and 3d.  In
 *      other words, it only plots data on level zero.  It can
 *      handle AMR in 1d.
 *
 *******************************************************************
 */


int main(int argc, char *argv[])
{
    /*
     * Initialize tbox::MPI and SAMRAI, enable logging, and process command line.
     */
    
    tbox::SAMRAI_MPI::init(&argc, &argv);
    tbox::SAMRAIManager::initialize();
    tbox::SAMRAIManager::startup();
    const tbox::SAMRAI_MPI& mpi(tbox::SAMRAI_MPI::getSAMRAIWorld());
    
    std::string input_filename;
    std::string restart_read_dirname;
    int restore_num = 0;
    
    bool is_from_restart = false;
    
    if ((argc != 2) && (argc != 4))
    {
        tbox::pout << "USAGE:  "
                   << argv[0]
                   << " <input filename> "
                   << "<restart dir> <restore number> [options]\n"
                   << "  options:\n"
                   << "  none at this time"
                   << std::endl;
        tbox::SAMRAI_MPI::abort();
        return -1;
    }
    else
    {
        input_filename = argv[1];
        if (argc == 4)
        {
            restart_read_dirname = argv[2];
            restore_num = atoi(argv[3]);
      
            is_from_restart = true;
        }
    }
    
    tbox::plog << "input_filename = " << input_filename << std::endl;
    tbox::plog << "restart_read_dirname = " << restart_read_dirname << std::endl;
    tbox::plog << "restore_num = " << restore_num << std::endl;
      
    /*
     * Create input database and parse all data in input file.
     */
    
    boost::shared_ptr<tbox::InputDatabase> input_db(new tbox::InputDatabase("input_db"));
    tbox::InputManager::getManager()->parseInputFile(input_filename, input_db);
    
    /*
     * Retrieve "GlobalInputs" section of the input database and set
     * values accordingly.
     */
    
    if (input_db->keyExists("GlobalInputs"))
    {
        boost::shared_ptr<tbox::Database> global_db(input_db->getDatabase("GlobalInputs"));
        
        if (global_db->keyExists("call_abort_in_serial_instead_of_exit"))
        {
            bool flag = global_db->getBool("call_abort_in_serial_instead_of_exit");
            tbox::SAMRAI_MPI::setCallAbortInSerialInsteadOfExit(flag);
        }
    }
    
    /*
     * Retrieve "Main" section of the input database.  First, read dump
     * information, which is used for writing plot files.  Second, if
     * proper restart information was given on command line, and the
     * restart interval is non-zero, create a restart database.
     */
    
    boost::shared_ptr<tbox::Database> main_db(input_db->getDatabase("Main"));
    
    const tbox::Dimension dim(static_cast<unsigned short>(main_db->getInteger("dim")));
    
    const std::string base_name = main_db->getStringWithDefault("base_name", "unnamed");
    
    const std::string log_filename = main_db->getStringWithDefault("log_filename", base_name + ".log");
    
    bool log_all_nodes = false;
    if (main_db->keyExists("log_all_nodes"))
    {
        log_all_nodes = main_db->getBool("log_all_nodes");
    }
    if (log_all_nodes)
    {
        tbox::PIO::logAllNodes(log_filename);
    }
    else
    {
        tbox::PIO::logOnlyNodeZero(log_filename);
    }
    
#ifdef _OPENMP
    tbox::plog << "Compiled with OpenMP version "
               << _OPENMP
               << ".  Running with "
               << omp_get_max_threads()
               << " threads."
               << std::endl;
#else
    tbox::plog << "Compiled without OpenMP.\n";
#endif
    
    int viz_dump_interval = 0;
    if (main_db->keyExists("viz_dump_interval"))
    {
        viz_dump_interval = main_db->getInteger("viz_dump_interval");
    }
    
    const std::string visit_dump_dirname =
        main_db->getStringWithDefault("viz_dump_dirname", base_name + ".visit");
    
    int visit_number_procs_per_file = 1;
    if (viz_dump_interval > 0)
    {
        if (main_db->keyExists("visit_number_procs_per_file"))
        {
           visit_number_procs_per_file = main_db->getInteger("visit_number_procs_per_file");
        }
    }
    
    int restart_interval = 0;
    if (main_db->keyExists("restart_interval"))
    {
        restart_interval = main_db->getInteger("restart_interval");
    }
    
    const std::string restart_write_dirname =
        main_db->getStringWithDefault("restart_write_dirname",
                                      base_name + ".restart");
    
    bool use_refined_timestepping = true;
    if (main_db->keyExists("timestepping"))
    {
        std::string timestepping_method = main_db->getString("timestepping");
        if (timestepping_method == "SYNCHRONIZED")
        {
            use_refined_timestepping = false;
        }
    }
    
    const bool write_restart = (restart_interval > 0)
                                && !(restart_write_dirname.empty());
    
    /*
     * Get restart manager and root restart database.  If run is from
     * restart, open the restart file.
     */
    
    tbox::RestartManager* restart_manager = tbox::RestartManager::getManager();
    
#ifdef HAVE_SILO
    /*
     * If SILO is present then use SILO as the file storage format
     * for this example, otherwise it will default to HDF5.
     */
    boost::shared_ptr<tbox::SiloDatabaseFactory> silo_database_factory(
        new tbox::SiloDatabaseFactory());
    restart_manager->setDatabaseFactory(silo_database_factory);
#endif
    
    if (is_from_restart)
    {
        restart_manager->openRestartFile(
            restart_read_dirname,
            restore_num,
            mpi.getSize());
    }
    
    /*
     * Setup the timer manager to trace timing statistics during execution
     * of the code.  The list of timers is given in the tbox::TimerManager
     * section of the input file.  Timing information is stored in the
     * restart file.  Timers will automatically be initialized to their
     * previous state if the run is restarted, unless they are explicitly
     * reset using the tbox::TimerManager::resetAllTimers() routine.
     */
    
    tbox::TimerManager::createManager(input_db->getDatabase("TimerManager"));
    
    /*
     * Create major algorithm and data objects which comprise application.
     * Each object is initialized either from input data or restart
     * files, or a combination of both.  Refer to each class constructor
     * for details.  For more information on the composition of objects
     * and the roles they play in this application, see comments at top of file.
     */
       
    boost::shared_ptr<geom::CartesianGridGeometry> grid_geometry(
        new geom::CartesianGridGeometry(
            dim,
            "CartesianGeometry",
            input_db->getDatabase("CartesianGeometry")));
    
    boost::shared_ptr<hier::PatchHierarchy> patch_hierarchy(
        new hier::PatchHierarchy(
            "PatchHierarchy",
            grid_geometry,
            input_db->getDatabase("PatchHierarchy")));
        
    Euler* euler_model = new Euler(
        "Euler",
        dim,
        input_db->getDatabase("Euler"),
        grid_geometry);
    
    boost::shared_ptr<RungeKuttaLevelIntegrator> RK_level_integrator(
        new RungeKuttaLevelIntegrator(
            "RungeKuttaLevelIntegrator",
            input_db->getDatabase("RungeKuttaLevelIntegrator"),
            euler_model,
            use_refined_timestepping));
    
    boost::shared_ptr<mesh::StandardTagAndInitialize> error_detector(
        new mesh::StandardTagAndInitialize(
            "StandardTagAndInitialize",
            RK_level_integrator.get(),
            input_db->getDatabase("StandardTagAndInitialize")));
    
    boost::shared_ptr<mesh::BergerRigoutsos> box_generator(
        new mesh::BergerRigoutsos(
            dim,
            input_db->getDatabaseWithDefault(
                "BergerRigoutsos",
                boost::shared_ptr<tbox::Database>())));
    
    boost::shared_ptr<mesh::TreeLoadBalancer> load_balancer(
        new mesh::TreeLoadBalancer(
            dim,
            "LoadBalancer",
            input_db->getDatabase("LoadBalancer"),
            boost::shared_ptr<tbox::RankTreeStrategy>(new tbox::BalancedDepthFirstTree)));
    
    load_balancer->setSAMRAI_MPI(tbox::SAMRAI_MPI::getSAMRAIWorld());
    
    boost::shared_ptr<mesh::GriddingAlgorithm> gridding_algorithm(
        new mesh::GriddingAlgorithm(
            patch_hierarchy,
            "GriddingAlgorithm",
            input_db->getDatabase("GriddingAlgorithm"),
            error_detector,
            box_generator,
            load_balancer));
    
    boost::shared_ptr<algs::TimeRefinementIntegrator> time_integrator(
        new algs::TimeRefinementIntegrator(
            "TimeRefinementIntegrator",
            input_db->getDatabase("TimeRefinementIntegrator"),
            patch_hierarchy,
            RK_level_integrator,
            gridding_algorithm));
    
    /*
     * Set up Visualization writer(s).  Note that the Euler application
     * creates some derived data quantities so we register the Euler model
     * as a derived data writer.  If no derived data is written, this step
     * is not necessary.
     */
#ifdef HAVE_HDF5
    boost::shared_ptr<appu::VisItDataWriter> visit_data_writer(
        new appu::VisItDataWriter(
            dim,
            "Euler VisIt Writer",
            visit_dump_dirname,
            visit_number_procs_per_file));
    
    euler_model->registerVisItDataWriter(visit_data_writer);
#endif
    
    /*
     * Initialize hierarchy configuration and data on all patches.
     * Then, close restart file and write initial state for visualization.
     */
    
    double dt_now = time_integrator->initializeHierarchy();
    
    tbox::RestartManager::getManager()->closeRestartFile();
    
    /*
     * After creating all objects and initializing their state, we
     * print the input database and variable database contents
     * to the log file.
     */
    
    tbox::plog << "\nCheck Euler data... " << std::endl;
    euler_model->printClassData(tbox::plog);
    
    tbox::plog << "\nCheck Runge-Kutta integrator data..." << std::endl;
    RK_level_integrator->printClassData(tbox::plog);
    
    /*
     * Create timers for measuring I/O.
     */
    boost::shared_ptr<tbox::Timer> t_write_viz(
        tbox::TimerManager::getManager()->getTimer("apps::main::write_viz"));
    boost::shared_ptr<tbox::Timer> t_write_restart(
        tbox::TimerManager::getManager()->getTimer(
            "apps::main::write_restart"));
    
    t_write_viz->start();
#ifdef HAVE_HDF5
    if (viz_dump_interval > 0)
    {
        visit_data_writer->writePlotData(
            patch_hierarchy,
            time_integrator->getIntegratorStep(),
            time_integrator->getIntegratorTime());
    }
#endif
    t_write_viz->stop();
    
    /*
     * Time step loop.  Note that the step count and integration
     * time are maintained by algs::TimeRefinementIntegrator.
     */
    
    double loop_time = time_integrator->getIntegratorTime();
    double loop_time_end = time_integrator->getEndTime();
    
    while (loop_time < loop_time_end && time_integrator->stepsRemaining())
    {
        int iteration_num = time_integrator->getIntegratorStep() + 1;
    
        tbox::pout << "++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
        tbox::pout << "At begining of timestep # " << iteration_num - 1 << std::endl;
        tbox::pout << "Simulation time is " << loop_time << std::endl;
        tbox::pout << "Current dt is " << dt_now << std::endl;
    
        double dt_new = time_integrator->advanceHierarchy(dt_now);
    
        loop_time += dt_now;
        dt_now = dt_new;
        
        tbox::pout << "At end of timestep # " << iteration_num - 1 << std::endl;
        tbox::pout << "Simulation time is " << loop_time << std::endl;
        euler_model->printDataStatistics(tbox::pout, patch_hierarchy);
        tbox::pout << "++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
    
        /*
         * At specified intervals, write restart files.
         */
        if (write_restart)
        {
            if ((iteration_num % restart_interval) == 0)
            {
                t_write_restart->start();
                tbox::RestartManager::getManager()->
                    writeRestartFile(restart_write_dirname,
                                     iteration_num);
                t_write_restart->stop();
            }
        }
        
        /*
         * At specified intervals, write out data files for plotting.
         */
        t_write_viz->start();
#ifdef HAVE_HDF5
        if ((viz_dump_interval > 0) && (iteration_num % viz_dump_interval) == 0)
        {
            visit_data_writer->writePlotData(
                patch_hierarchy,
                iteration_num,
                loop_time);
        }
#endif
        t_write_viz->stop();
    }
    
    /*
     * Write out data of the last time step.
     */
    int iteration_num = time_integrator->getIntegratorStep();
    if ((viz_dump_interval > 0) && (iteration_num % viz_dump_interval) != 0)
    {
        visit_data_writer->writePlotData(
            patch_hierarchy,
            iteration_num,
            loop_time);
    }
    
    tbox::plog << "GriddingAlgorithm statistics:\n";
    gridding_algorithm->printStatistics();
    
    /*
     * Output timer results.
     */
    tbox::TimerManager::getManager()->print(tbox::plog);
    
    /*
     * At conclusion of simulation, deallocate objects.
     */
    patch_hierarchy.reset();
    grid_geometry.reset();
    
    box_generator.reset();
    load_balancer.reset();
    RK_level_integrator.reset();
    error_detector.reset();
    gridding_algorithm.reset();
    time_integrator.reset();
#ifdef HAVE_HDF5
    visit_data_writer.reset();
#endif
    
    if (euler_model)
        delete euler_model;
    
    tbox::SAMRAIManager::shutdown();
    tbox::SAMRAIManager::finalize();
    tbox::SAMRAI_MPI::finalize();
   
    return 0;
}