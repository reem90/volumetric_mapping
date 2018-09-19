/*
Copyright (c) 2015, Helen Oleynikova, ETH Zurich, Switzerland
You can contact the author at <helen dot oleynikova at mavt dot ethz dot ch>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of ETHZ-ASL nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ETHZ-ASL BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "octomap_world/octomap_world.h"
#include <glog/logging.h>
#include <octomap_msgs/conversions.h>
#include <octomap_ros/conversions.h>
#include <pcl/conversions.h>
#include <pcl/filters/filter.h>
#include <pcl_ros/transforms.h>
#include <iostream>
#include <numeric>
#include <cmath>
#include <octomap/LabelOcTree.h>

#define SQ(x) ((x)*(x))
#define SQRT2 0.70711

namespace volumetric_mapping {






bool flag_first_time = true ;
int s ;
int pre_index ;
//*/ Convenience functions for octomap point <-> eigen conversions.
octomap::point3d pointEigenToOctomap(const Eigen::Vector3d& point) {
    return octomap::point3d(point.x(), point.y(), point.z());
}

Eigen::Vector3d pointOctomapToEigen(const octomap::point3d& point) {
    return Eigen::Vector3d(point.x(), point.y(), point.z());
}

// Create a default parameters object and call the other constructor with it.
OctomapWorld::OctomapWorld() : OctomapWorld(OctomapParameters()) {}

// Creates an octomap with the correct parameters.
OctomapWorld::OctomapWorld(const OctomapParameters& params)
    : robot_size_(Eigen::Vector3d::Zero())
{
    setOctomapParameters(params);
}

void OctomapWorld::resetMap() {
    if (!octree_) {
        octree_.reset(new octomap::LabelOcTree(params_.resolution));
    }
    octree_->clear();
}
void OctomapWorld::prune() { octree_->prune(); }

void OctomapWorld::setOctomapParameters(const OctomapParameters& params) {
    if (octree_) {
        if (octree_->getResolution() != params.resolution) {
            LOG(WARNING) << "Octomap resolution has changed! Resetting tree!";
            octree_.reset(new octomap::LabelOcTree(params.resolution));
        }
    }
    else {
        octree_.reset(new octomap::LabelOcTree(params.resolution));
    }

    octree_->setProbHit(params.probability_hit);
    octree_->setProbMiss(params.probability_miss);
    octree_->setClampingThresMin(params.threshold_min);
    octree_->setClampingThresMax(params.threshold_max);
    octree_->setOccupancyThres(params.threshold_occupancy);
    octree_->enableChangeDetection(params.change_detection_enabled);

    // Copy over all the parameters for future use (some are not used just for
    // creating the octree).
    params_ = params;
}

void OctomapWorld::insertPointcloudIntoMapImpl(
        const Transformation& T_G_sensor,
        const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud) {
    // Remove NaN values, if any.
    std::vector<int> indices;
    pcl::removeNaNFromPointCloud(*cloud, *cloud, indices);

    // First, rotate the pointcloud into the world frame.
    pcl::transformPointCloud(*cloud, *cloud,
                             T_G_sensor.getTransformationMatrix());
    const octomap::point3d p_G_sensor =
            pointEigenToOctomap(T_G_sensor.getPosition());

    // Then add all the rays from this pointcloud.
    // We do this as a batch operation - so first get all the keys in a set, then
    // do the update in batch.
    octomap::KeySet free_cells, occupied_cells;
    for (pcl::PointCloud<pcl::PointXYZ>::const_iterator it = cloud->begin();
         it != cloud->end(); ++it) {
        const octomap::point3d p_G_point(it->x, it->y, it->z);
        // First, check if we've already checked this.
        octomap::OcTreeKey key = octree_->coordToKey(p_G_point);
        if (occupied_cells.find(key) == occupied_cells.end()) {
            // Check if this is within the allowed sensor range.
            castRay(p_G_sensor, p_G_point, &free_cells, &occupied_cells);
        }
    }

    // Apply the new free cells and occupied cells from
    updateOccupancy(&free_cells, &occupied_cells);

}


void OctomapWorld::insertPointcloudColorIntoMapImpl(
        const Transformation& T_G_sensor,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud) {
    // Remove NaN values, if any.
    std::vector<int> indices;
    pcl::removeNaNFromPointCloud(*cloud, *cloud, indices);

    ROS_INFO("Sensor Point %f %f %f %f   %f   %f   %f ",T_G_sensor.getPosition().x(),T_G_sensor.getPosition().y(),T_G_sensor.getPosition().z(), T_G_sensor.getRotation().x() , T_G_sensor.getRotation().y() ,T_G_sensor.getRotation().z() ,T_G_sensor.getRotation().w() );

    //std::cout << "Transformation Matrix" < T_G_sensor.getTransformationMatrix() << std::endl << std::flush ; 
    // First, rotate the pointcloud into the world frame.
    pcl::transformPointCloud(*cloud, *cloud,
                             T_G_sensor.getTransformationMatrix());
    const octomap::point3d p_G_sensor =
            pointEigenToOctomap(T_G_sensor.getPosition());

    // Then add all the rays from this pointcloud.
    // We do this as a batch operation - so first get all the keys in a set, then
    // do the update in batch.
    octomap::KeySet free_cells, occupied_cells;
    for (pcl::PointCloud<pcl::PointXYZRGB>::const_iterator it = cloud->begin();
         it != cloud->end(); ++it) {

        const octomap::point3d p_G_point(it->x, it->y, it->z);
        // First, check if we've already checked this.
        octomap::OcTreeKey key = octree_->coordToKey(p_G_point);
    
        if (occupied_cells.find(key) == occupied_cells.end()) {
            // Check if this is within the allowed sensor range.
            castRay(p_G_sensor, p_G_point, &free_cells, &occupied_cells);
        }
    }

    // Apply the new free cells and occupied cells from
    updateOccupancy(&free_cells, &occupied_cells);
    
    
    for (pcl::PointCloud<pcl::PointXYZRGB>::const_iterator it = cloud->begin();
         it != cloud->end(); ++it) {
        const octomap::point3d p_G_point(it->x, it->y, it->z);
        const octomap::point3d p_G_color((int)it->r,(int)it->g,(int)it->b) ;

        octomap::point3d direction = octomap::point3d(0,0,0) ;
        octomap::point3d obstacle (0,0,0);
        direction.x() = p_G_point.x() - p_G_sensor.x() ;
        direction.y() = p_G_point.y() - p_G_sensor.y() ;
        direction.z() = p_G_point.z() - p_G_sensor.z() ;

        int class_type_1 = identifyClass(p_G_color);

        int class_index  = -1  ;
        double certainty_val = introduceNoise(class_type_1,class_index) ;

        int t =  octree_->castRay(p_G_sensor, direction, obstacle,false,0);

        if(t)
        {
            octomap::LabelOcTreeNode* node = octree_->search(obstacle);
            
            if (octree_->isNodeOccupied(node))
            {
               // ROS_INFO("******Debug2******") ;
                updateSingleVoxelInfo(node, class_index , certainty_val ) ;
               // ROS_INFO("******update******") ;

            }
        }
//        else
//            ROS_INFO("cast ray return false");
    }


    geometry_msgs::Pose p ;
    p.position.x =  T_G_sensor.getPosition().x();
    p.position.y =  T_G_sensor.getPosition().y();
    p.position.z =  T_G_sensor.getPosition().z();
    p.orientation.x = T_G_sensor.getRotation().x();
    p.orientation.y = T_G_sensor.getRotation().y();
    p.orientation.z = T_G_sensor.getRotation().z();
    p.orientation.w = T_G_sensor.getRotation().w();
    //ROS_INFO("******Number of Visits ****** %f   %f   %f   %f   %f   %f   %f", p.position.x,p.position.y , p.position.z, p.orientation.x , p.orientation.y , p.orientation.z , p.orientation.w ) ;

    UpdateNumberofVisits(p);
    //ROS_INFO("v-----------------------------------------------");
    updateIntrestValue() ;
    updateOccupancy(&free_cells, &occupied_cells);
    //ROS_INFO("enddd-----------------------------------------------");
}


void OctomapWorld::insertProjectedDisparityIntoMapImpl(
        const Transformation& sensor_to_world, const cv::Mat& projected_points) {
    // Get the sensor origin in the world frame.
    Eigen::Vector3d sensor_origin_eigen = Eigen::Vector3d::Zero();
    sensor_origin_eigen = sensor_to_world * sensor_origin_eigen;
    octomap::point3d sensor_origin = pointEigenToOctomap(sensor_origin_eigen);

    octomap::KeySet free_cells, occupied_cells;
    for (int v = 0; v < projected_points.rows; ++v) {
        const cv::Vec3f* row_pointer = projected_points.ptr<cv::Vec3f>(v);

        for (int u = 0; u < projected_points.cols; ++u) {
            // Check whether we're within the correct range for disparity.
            if (!isValidPoint(row_pointer[u]) || row_pointer[u][2] < 0) {
                continue;
            }
            Eigen::Vector3d point_eigen(row_pointer[u][0], row_pointer[u][1],
                    row_pointer[u][2]);

            point_eigen = sensor_to_world * point_eigen;
            octomap::point3d point_octomap = pointEigenToOctomap(point_eigen);

            // First, check if we've already checked this.
            octomap::OcTreeKey key = octree_->coordToKey(point_octomap);

            if (occupied_cells.find(key) == occupied_cells.end()) {
                // Check if this is within the allowed sensor range.
                castRay(sensor_origin, point_octomap, &free_cells, &occupied_cells);
            }
        }
    }
    updateOccupancy(&free_cells, &occupied_cells);
}


int OctomapWorld::castRay(const octomap::point3d& sensor_origin,
                          const octomap::point3d& point,
                          octomap::KeySet* free_cells,
                          octomap::KeySet* occupied_cells) const {

    //ROS_INFO("CAST                   RAY ") ;
    CHECK_NOTNULL(free_cells);
    CHECK_NOTNULL(occupied_cells);
    int res = 0;
    if (params_.sensor_max_range < 0.0 ||
            (point - sensor_origin).norm() <= params_.sensor_max_range) {
        // Cast a ray to compute all the free cells.
        octomap::KeyRay key_ray;
        if (octree_->computeRayKeys(sensor_origin, point, key_ray)) {
            if (params_.max_free_space == 0.0) {
                free_cells->insert(key_ray.begin(), key_ray.end());
            } else {
                for (const auto& key : key_ray) {
                    octomap::point3d voxel_coordinate = octree_->keyToCoord(key);
                    if ((voxel_coordinate - sensor_origin).norm() <
                            params_.max_free_space ||
                            voxel_coordinate.z() >
                            (sensor_origin.z() - params_.min_height_free_space)) {
                        free_cells->insert(key);
                    }
                }
            }
        }
        // Mark endpoing as occupied.
        octomap::OcTreeKey key;
        if (octree_->coordToKeyChecked(point, key)) {
            occupied_cells->insert(key);
            res=1;
        }
    } else {
        // If the ray is longer than the max range, just update free space.
        octomap::point3d new_end =
                sensor_origin +
                (point - sensor_origin).normalized() * params_.sensor_max_range;
        octomap::KeyRay key_ray;
        if (octree_->computeRayKeys(sensor_origin, new_end, key_ray)) {
            if (params_.max_free_space == 0.0) {
                free_cells->insert(key_ray.begin(), key_ray.end());
            } else {
                for (const auto& key : key_ray) {
                    octomap::point3d voxel_coordinate = octree_->keyToCoord(key);
                    if ((voxel_coordinate - sensor_origin).norm() <
                            params_.max_free_space ||
                            voxel_coordinate.z() >
                            (sensor_origin.z() - params_.min_height_free_space)) {
                        free_cells->insert(key);
                    }
                }
            }
        }
    }
    return res ;
}


bool OctomapWorld::isValidPoint(const cv::Vec3f& point) const {
    // Check both for disparities explicitly marked as invalid (where OpenCV maps
    // pt.z to MISSING_Z) and zero disparities (point mapped to infinity).
    return point[2] != 10000.0f && !std::isinf(point[2]);
}

void OctomapWorld::updateOccupancy(octomap::KeySet* free_cells,
                                   octomap::KeySet* occupied_cells) {
    CHECK_NOTNULL(free_cells);
    CHECK_NOTNULL(occupied_cells);
    //ROS_ERROR("updateOccupancy"); 
    // Mark occupied cells.
    for (octomap::KeySet::iterator it = occupied_cells->begin(),
         end = occupied_cells->end();
         it != end; it++) {
        octree_->updateNode(*it, true);

        // Remove any occupied cells from free cells - assume there are far fewer
        // occupied cells than free cells, so this is much faster than checking on
        // every free cell.
        if (free_cells->find(*it) != free_cells->end()) {
            free_cells->erase(*it);
        }
    }

    // Mark free cells.
    for (octomap::KeySet::iterator it = free_cells->begin(),
         end = free_cells->end();
         it != end; ++it) {
        octree_->updateNode(*it, false);
    }
    octree_->updateInnerOccupancy();
}

void OctomapWorld::enableTreatUnknownAsOccupied() {
    params_.treat_unknown_as_occupied = true;
}

void OctomapWorld::disableTreatUnknownAsOccupied() {
    params_.treat_unknown_as_occupied = false;
}

OctomapWorld::CellStatus OctomapWorld::getCellStatusBoundingBox(
        const Eigen::Vector3d& point,
        const Eigen::Vector3d& bounding_box_size) const {
    // First case: center point is unknown or occupied. Can just quit.
    CellStatus center_status = getCellStatusPoint(point);
    if (center_status != CellStatus::kFree) {
        return center_status;
    }


    // The returns are flipped in s
    // Also if center is outside of the bounds.
    octomap::OcTreeKey key;
    if (!octree_->coordToKeyChecked(pointEigenToOctomap(point), key)) {
        if (params_.treat_unknown_as_occupied) {
            return CellStatus::kOccupied;
        } else {
            return CellStatus::kUnknown;
        }
    }

    // Now we have to iterate over everything in the bounding box.
    Eigen::Vector3d bbx_min_eigen = point - bounding_box_size / 2;
    Eigen::Vector3d bbx_max_eigen = point + bounding_box_size / 2;

    octomap::point3d bbx_min = pointEigenToOctomap(bbx_min_eigen);
    octomap::point3d bbx_max = pointEigenToOctomap(bbx_max_eigen);

    for (octomap::LabelOcTree::leaf_bbx_iterator
         iter = octree_->begin_leafs_bbx(bbx_min, bbx_max),
         end = octree_->end_leafs_bbx();
         iter != end; ++iter) {
        Eigen::Vector3d cube_center(iter.getX(), iter.getY(), iter.getZ());
        int depth_level = iter.getDepth();
        double cube_size = octree_->getNodeSize(depth_level);

        // Check if it is really inside bounding box, since leaf_bbx_iterator begins
        // "too early"
        Eigen::Vector3d cube_lower_bound =
                cube_center - (cube_size / 2) * Eigen::Vector3d::Ones();
        Eigen::Vector3d cube_upper_bound =
                cube_center + (cube_size / 2) * Eigen::Vector3d::Ones();
        if (cube_upper_bound.x() < bbx_min.x() ||
                cube_lower_bound.x() > bbx_max.x() ||
                cube_upper_bound.y() < bbx_min.y() ||
                cube_lower_bound.y() > bbx_max.y() ||
                cube_upper_bound.z() < bbx_min.z() ||
                cube_lower_bound.z() > bbx_max.z()) {
            continue;
        }

        if (octree_->isNodeOccupied(*iter)) {
            if (params_.filter_speckles && isSpeckleNode(iter.getKey())) {
                continue;
            } else {
                return CellStatus::kOccupied;
            }
        }
    }

    // The above only returns valid nodes - we should check for unknown nodes as
    // well.
    octomap::point3d_list unknown_centers;
    octree_->getUnknownLeafCenters(unknown_centers, bbx_min, bbx_max);
    if (unknown_centers.size() > 0) {
        if (params_.treat_unknown_as_occupied) {
            return CellStatus::kOccupied;
        } else {
            return CellStatus::kUnknown;
        }
    }
    return CellStatus::kFree;
}

OctomapWorld::CellStatus OctomapWorld::getCellStatusPoint(
        const Eigen::Vector3d& point) const {
    octomap::OcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
    if (node == NULL) {
        if (params_.treat_unknown_as_occupied) {
            return CellStatus::kOccupied;
        } else {
            return CellStatus::kUnknown;
        }
    } else if (octree_->isNodeOccupied(node)) {
        return CellStatus::kOccupied;
    } else {
        return CellStatus::kFree;
    }
}

OctomapWorld::CellStatus OctomapWorld::getCellProbabilityPoint(
        const Eigen::Vector3d& point, double* probability) const {
    octomap::OcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
    if (node == NULL) {
        if (probability) {
            *probability = -1.0;
        }
        return CellStatus::kUnknown;
    } else {
        if (probability) {
            *probability = node->getOccupancy();
        }
        if (octree_->isNodeOccupied(node)) {
            return CellStatus::kOccupied;
        } else {
            return CellStatus::kFree;
        }
    }
}

OctomapWorld::CellStatus OctomapWorld::getLineStatus(
        const Eigen::Vector3d& start, const Eigen::Vector3d& end) const {
    // Get all node keys for this line.
    // This is actually a typedef for a vector of OcTreeKeys.
    // Can't use the key_ray_ temp member here because this is a const function.
    octomap::KeyRay key_ray;
    octree_->computeRayKeys(pointEigenToOctomap(start), pointEigenToOctomap(end),
                            key_ray);

    // Now check if there are any unknown or occupied nodes in the ray.
    for (octomap::OcTreeKey key : key_ray) {
        octomap::OcTreeNode* node = octree_->search(key);
        if (node == NULL) {
            if (params_.treat_unknown_as_occupied) {
                return CellStatus::kOccupied;
            } else {
                return CellStatus::kUnknown;
            }
        } else if (octree_->isNodeOccupied(node)) {
            return CellStatus::kOccupied;
        }
    }
    return CellStatus::kFree;
}


OctomapWorld::CellStatus OctomapWorld::getVisibility(
        const Eigen::Vector3d& view_point, const Eigen::Vector3d& voxel_to_test,
        bool stop_at_unknown_cell) const {
    // Get all node keys for this line.
    // This is actually a typedef for a vector of OcTreeKeys.
    // Can't use the key_ray_ temp member here because this is a const function.
    octomap::KeyRay key_ray;

    octree_->computeRayKeys(pointEigenToOctomap(view_point),
                            pointEigenToOctomap(voxel_to_test), key_ray);

    const octomap::OcTreeKey& voxel_to_test_key =
            octree_->coordToKey(pointEigenToOctomap(voxel_to_test));

    // Now check if there are any unknown or occupied nodes in the ray,
    // except for the voxel_to_test key.
    for (octomap::OcTreeKey key : key_ray) {
        if (key != voxel_to_test_key) {
            octomap::OcTreeNode* node = octree_->search(key);
            if (node == NULL) {
                if (stop_at_unknown_cell) {
                    return CellStatus::kUnknown;
                }
            } else if (octree_->isNodeOccupied(node)) {
                return CellStatus::kOccupied;
            }
        }
    }
    return CellStatus::kFree;
}

OctomapWorld::CellStatus OctomapWorld::getLineStatusBoundingBox(
        const Eigen::Vector3d& start, const Eigen::Vector3d& end,
        const Eigen::Vector3d& bounding_box_size) const {
    // TODO(helenol): Probably best way would be to get all the coordinates along
    // the line, then make a set of all the OcTreeKeys in all the bounding boxes
    // around the nodes... and then just go through and query once.
    const double epsilon = 0.001;  // Small offset
    CellStatus ret = CellStatus::kFree;
    const double& resolution = getResolution();
    ROS_INFO("getLineStatusBoundingBox resolution",resolution);

    // Check corner connections and depending on resolution also interior:
    // Discretization step is smaller than the octomap resolution, as this way
    // no cell can possibly be missed
    double x_disc = bounding_box_size.x() /
            ceil((bounding_box_size.x() + epsilon) / resolution);
    double y_disc = bounding_box_size.y() /
            ceil((bounding_box_size.y() + epsilon) / resolution);
    double z_disc = bounding_box_size.z() /
            ceil((bounding_box_size.z() + epsilon) / resolution);

    // Ensure that resolution is not infinit
    if (x_disc <= 0.0) x_disc = 1.0;
    if (y_disc <= 0.0) y_disc = 1.0;
    if (z_disc <= 0.0) z_disc = 1.0;

    const Eigen::Vector3d bounding_box_half_size = bounding_box_size * 0.5;

    for (double x = -bounding_box_half_size.x(); x <= bounding_box_half_size.x();
         x += x_disc) {
        for (double y = -bounding_box_half_size.y();
             y <= bounding_box_half_size.y(); y += y_disc) {
            for (double z = -bounding_box_half_size.z();
                 z <= bounding_box_half_size.z(); z += z_disc) {
                Eigen::Vector3d offset(x, y, z);
                ret = getLineStatus(start + offset, end + offset);
                if (ret != CellStatus::kFree) {
                    return ret;
                }
            }
        }
    }
    return CellStatus::kFree;
}

double OctomapWorld::getResolution() const { return octree_->getResolution(); }
void OctomapWorld::setFree(const Eigen::Vector3d& position,
                           const Eigen::Vector3d& bounding_box_size,
                           const BoundHandling& insertion_method) {
    setLogOddsBoundingBox(position, bounding_box_size,
                          octree_->getClampingThresMinLog(), insertion_method);
}

void OctomapWorld::setFree(const std::vector<Eigen::Vector3d>& positions,
                           const Eigen::Vector3d& bounding_box_size,
                           const BoundHandling& insertion_method) {
    setLogOddsBoundingBox(positions, bounding_box_size,
                          octree_->getClampingThresMinLog(), insertion_method);
}

void OctomapWorld::setOccupied(const Eigen::Vector3d& position,
                               const Eigen::Vector3d& bounding_box_size,
                               const BoundHandling& insertion_method) {
    setLogOddsBoundingBox(position, bounding_box_size,
                          octree_->getClampingThresMaxLog(), insertion_method);
}

void OctomapWorld::setOccupied(const std::vector<Eigen::Vector3d>& positions,
                               const Eigen::Vector3d& bounding_box_size,
                               const BoundHandling& insertion_method) {
    setLogOddsBoundingBox(positions, bounding_box_size,
                          octree_->getClampingThresMaxLog(), insertion_method);
}

void OctomapWorld::setBordersOccupied(const Eigen::Vector3d& cropping_size) {
    // Crop map size by setting borders occupied
    const bool lazy_eval = true;
    const double log_odds_value = octree_->getClampingThresMaxLog();
    octomap::KeySet occupied_keys;

    Eigen::Vector3d map_center = getMapCenter();
    Eigen::Vector3d map_size = getMapSize();
    Eigen::Vector3d bbx_center;
    Eigen::Vector3d bbx_size;

    bbx_size = map_size;
    bbx_size.x() = cropping_size.x() / 2;
    bbx_center = map_center;
    bbx_center.x() = map_center.x() - map_size.x() / 2 + cropping_size.x() / 4;
    getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                       BoundHandling::kIncludePartialBoxes);
    bbx_center.x() = map_center.x() + map_size.x() / 2 - cropping_size.x() / 4;
    getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                       BoundHandling::kIncludePartialBoxes);

    bbx_size = map_size;
    bbx_size.y() = cropping_size.y() / 2;
    bbx_center = map_center;
    bbx_center.y() = map_center.y() - map_size.y() / 2 + cropping_size.y() / 4;
    getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                       BoundHandling::kIncludePartialBoxes);
    bbx_center.y() = map_center.y() + map_size.y() / 2 - cropping_size.y() / 4;
    getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                       BoundHandling::kIncludePartialBoxes);

    bbx_size = map_size;
    bbx_size.z() = cropping_size.z() / 2;
    bbx_center = map_center;
    bbx_center.z() = map_center.z() - map_size.z() / 2 + cropping_size.z() / 4;
    getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                       BoundHandling::kIncludePartialBoxes);
    bbx_center.z() = map_center.z() + map_size.z() / 2 - cropping_size.z() / 4;
    getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                       BoundHandling::kIncludePartialBoxes);

    // Set all infeasible points occupied
    for (octomap::OcTreeKey key : occupied_keys) {
        octree_->setNodeValue(octree_->keyToCoord(key), log_odds_value, lazy_eval);
    }

    if (lazy_eval) {
        //octree_->updateInnerOccupancy();
    }
    octree_->prune();
}

void OctomapWorld::getOccupiedPointCloud(
        pcl::PointCloud<pcl::PointXYZ>* output_cloud) const {
    CHECK_NOTNULL(output_cloud)->clear();
    unsigned int max_tree_depth = octree_->getTreeDepth();
    double resolution = octree_->getResolution();
    for (octomap::LabelOcTree::leaf_iterator it = octree_->begin_leafs();
         it != octree_->end_leafs(); ++it) {
        if (octree_->isNodeOccupied(*it)) {
            // If leaf is max depth add coordinates.
            if (max_tree_depth == it.getDepth()) {
                pcl::PointXYZ point(it.getX(), it.getY(), it.getZ());
                output_cloud->push_back(point);
            }
            // If leaf is not max depth it represents an occupied voxel with edge
            // length of 2^(max_tree_depth - leaf_depth) * resolution.
            // We use multiple points to visualize this filled volume.
            else {
                const unsigned int box_edge_length =
                        pow(2, max_tree_depth - it.getDepth() - 1);
                const double bbx_offset = box_edge_length * resolution - resolution / 2;
                Eigen::Vector3d bbx_offset_vec(bbx_offset, bbx_offset, bbx_offset);
                Eigen::Vector3d center(it.getX(), it.getY(), it.getZ());
                Eigen::Vector3d bbx_min = center - bbx_offset_vec;
                Eigen::Vector3d bbx_max = center + bbx_offset_vec;
                // Add small offset to avoid overshooting bbx_max.
                bbx_max += Eigen::Vector3d(0.001, 0.001, 0.001);
                for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
                     x_position += resolution) {
                    for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
                         y_position += resolution) {
                        for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
                             z_position += resolution) {
                            output_cloud->push_back(
                                        pcl::PointXYZ(x_position, y_position, z_position));
                        }
                    }
                }
            }
        }
    }
}


void OctomapWorld::getOccupiedPointcloudInBoundingBox(
        const Eigen::Vector3d& center, const Eigen::Vector3d& bounding_box_size,
        pcl::PointCloud<pcl::PointXYZ>* output_cloud,
        const BoundHandling& insertion_method) const {
    CHECK_NOTNULL(output_cloud);
    output_cloud->clear();

    const double resolution = octree_->getResolution();

    // Determine correct center of voxel.
    const Eigen::Vector3d center_corrected(
                resolution * std::floor(center.x() / resolution) + resolution / 2.0,
                resolution * std::floor(center.y() / resolution) + resolution / 2.0,
                resolution * std::floor(center.z() / resolution) + resolution / 2.0);

    Eigen::Vector3d bbx_min, bbx_max;

    if (insertion_method == BoundHandling::kDefault) {
        adjustBoundingBox(center_corrected, bounding_box_size, insertion_method,
                          &bbx_min, &bbx_max);
    } else {
        adjustBoundingBox(center, bounding_box_size, insertion_method, &bbx_min,
                          &bbx_max);
    }

    for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
         x_position += resolution) {
        for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
             y_position += resolution) {
            for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
                 z_position += resolution) {
                octomap::point3d point =
                        octomap::point3d(x_position, y_position, z_position);
                octomap::OcTreeKey key = octree_->coordToKey(point);
                octomap::OcTreeNode* node = octree_->search(key);
                if (node != NULL && octree_->isNodeOccupied(node)) {
                    output_cloud->push_back(
                                pcl::PointXYZ(point.x(), point.y(), point.z()));
                }
            }
        }
    }
}

void OctomapWorld::getAllFreeBoxes(
        std::vector<std::pair<Eigen::Vector3d, double>>* free_box_vector) const {
    const bool occupied_boxes = false;
    getAllBoxes(occupied_boxes, free_box_vector);
}

void OctomapWorld::getAllOccupiedBoxes(
        std::vector<std::pair<Eigen::Vector3d, double>>* occupied_box_vector)
const {
    const bool occupied_boxes = true;
    getAllBoxes(occupied_boxes, occupied_box_vector);
}

void OctomapWorld::getAllBoxes(
        bool occupied_boxes,
        std::vector<std::pair<Eigen::Vector3d, double>>* box_vector) const {
    box_vector->clear();
    box_vector->reserve(octree_->size());
    for (octomap::LabelOcTree::leaf_iterator it = octree_->begin_leafs(),
         end = octree_->end_leafs();
         it != end; ++it) {
        Eigen::Vector3d cube_center(it.getX(), it.getY(), it.getZ());
        int depth_level = it.getDepth();
        double cube_size = octree_->getNodeSize(depth_level);

        if (octree_->isNodeOccupied(*it) && occupied_boxes) {
            box_vector->emplace_back(cube_center, cube_size);
        } else if (!octree_->isNodeOccupied(*it) && !occupied_boxes) {
            box_vector->emplace_back(cube_center, cube_size);
        }
    }
}

void OctomapWorld::getFreeBoxesBoundingBox(
        const Eigen::Vector3d& position, const Eigen::Vector3d& bounding_box_size,
        std::vector<std::pair<Eigen::Vector3d, double>>* free_box_vector) const {
    const bool occupied_boxes = false;
    getBoxesBoundingBox(occupied_boxes, position, bounding_box_size,
                        free_box_vector);
}

void OctomapWorld::getOccupiedBoxesBoundingBox(
        const Eigen::Vector3d& position, const Eigen::Vector3d& bounding_box_size,
        std::vector<std::pair<Eigen::Vector3d, double>>* occupied_box_vector)
const {
    const bool occupied_boxes = true;
    getBoxesBoundingBox(occupied_boxes, position, bounding_box_size,
                        occupied_box_vector);
}

void OctomapWorld::getBoxesBoundingBox(
        bool occupied_boxes, const Eigen::Vector3d& position,
        const Eigen::Vector3d& bounding_box_size,
        std::vector<std::pair<Eigen::Vector3d, double>>* box_vector) const {
    box_vector->clear();
    if (bounding_box_size.maxCoeff() <= 0.0 || octree_->size() == 0) {
        return;
    }
    const Eigen::Vector3d max_boxes =
            bounding_box_size / octree_->getResolution();
    const int max_vector_size = std::ceil(max_boxes.x()) *
            std::ceil(max_boxes.y()) *
            std::ceil(max_boxes.z());
    box_vector->reserve(max_vector_size);

    const double epsilon = 0.001;  // Small offset to not hit boundary of nodes.
    Eigen::Vector3d epsilon_3d;
    epsilon_3d.setConstant(epsilon);

    Eigen::Vector3d bbx_min_eigen = position - bounding_box_size / 2 + epsilon_3d;
    Eigen::Vector3d bbx_max_eigen = position + bounding_box_size / 2 - epsilon_3d;

    octomap::point3d bbx_min = pointEigenToOctomap(bbx_min_eigen);
    octomap::point3d bbx_max = pointEigenToOctomap(bbx_max_eigen);

    for (octomap::LabelOcTree::leaf_bbx_iterator
         it = octree_->begin_leafs_bbx(bbx_min, bbx_max),
         end = octree_->end_leafs_bbx();
         it != end; ++it) {
        Eigen::Vector3d cube_center(it.getX(), it.getY(), it.getZ());
        int depth_level = it.getDepth();
        double cube_size = octree_->getNodeSize(depth_level);

        // Check if it is really inside bounding box, since leaf_bbx_iterator begins
        // "too early"
        Eigen::Vector3d cube_lower_bound =
                cube_center - (cube_size / 2) * Eigen::Vector3d::Ones();
        Eigen::Vector3d cube_upper_bound =
                cube_center + (cube_size / 2) * Eigen::Vector3d::Ones();
        if (cube_upper_bound.x() < bbx_min.x() ||
                cube_lower_bound.x() > bbx_max.x() ||
                cube_upper_bound.y() < bbx_min.y() ||
                cube_lower_bound.y() > bbx_max.y() ||
                cube_upper_bound.z() < bbx_min.z() ||
                cube_lower_bound.z() > bbx_max.z()) {
            continue;
        }

        if (octree_->isNodeOccupied(*it) && occupied_boxes) {
            box_vector->emplace_back(cube_center, cube_size);
        } else if (!octree_->isNodeOccupied(*it) && !occupied_boxes) {
            box_vector->emplace_back(cube_center, cube_size);
        }
    }
}

void OctomapWorld::setLogOddsBoundingBox(
        const Eigen::Vector3d& position, const Eigen::Vector3d& bounding_box_size,
        double log_odds_value, const BoundHandling& insertion_method) {
    std::vector<Eigen::Vector3d> positions;
    positions.push_back(position);
    setLogOddsBoundingBox(positions, bounding_box_size, log_odds_value,
                          insertion_method);
}

void OctomapWorld::setLogOddsBoundingBox(
        const std::vector<Eigen::Vector3d>& positions,
        const Eigen::Vector3d& bounding_box_size, double log_odds_value,
        const BoundHandling& insertion_method) {
    const bool lazy_eval = true;
    const double resolution = octree_->getResolution();
    Eigen::Vector3d bbx_min, bbx_max;

    for (const Eigen::Vector3d& position : positions) {
        adjustBoundingBox(position, bounding_box_size, insertion_method, &bbx_min,
                          &bbx_max);

        for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
             x_position += resolution) {
            for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
                 y_position += resolution) {
                for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
                     z_position += resolution) {
                    octomap::point3d point =
                            octomap::point3d(x_position, y_position, z_position);
                    octree_->setNodeValue(point, log_odds_value, lazy_eval);
                }
            }
        }
    }

    // This is necessary since lazy_eval is set to true.
    //octree_->updateInnerOccupancy();
}

bool OctomapWorld::getOctomapBinaryMsg(octomap_msgs::Octomap* msg) const {
    return octomap_msgs::binaryMapToMsg(*octree_, *msg);
}

bool OctomapWorld::getOctomapFullMsg(octomap_msgs::Octomap* msg) const {
    return octomap_msgs::fullMapToMsg(*octree_, *msg);
}

void OctomapWorld::setOctomapFromMsg(const octomap_msgs::Octomap& msg) {
    if (msg.binary) {
        setOctomapFromBinaryMsg(msg);
    } else {
        setOctomapFromFullMsg(msg);
    }
}

void OctomapWorld::setOctomapFromBinaryMsg(const octomap_msgs::Octomap& msg) {
    octree_.reset(
                dynamic_cast<octomap::LabelOcTree*>(octomap_msgs::binaryMsgToMap(msg)));
}

void OctomapWorld::setOctomapFromFullMsg(const octomap_msgs::Octomap& msg) {
    octree_.reset(
                dynamic_cast<octomap::LabelOcTree*>(octomap_msgs::fullMsgToMap(msg)));
}

bool OctomapWorld::loadOctomapFromFile(const std::string& filename) {
    return octree_->readBinary(filename);
}

bool OctomapWorld::writeOctomapToFile(const std::string& filename) {
    return octree_->writeBinary(filename);
}

bool OctomapWorld::writeOctomapToBinaryConst(std::ostream& s) const {
    return octree_->writeBinaryConst(s);
}

bool OctomapWorld::isSpeckleNode(const octomap::OcTreeKey& key) const {
    octomap::OcTreeKey current_key;
    // Search neighbors in a +/-1 key range cube. If there are neighbors, it's
    // not a speckle.
    bool neighbor_found = false;
    for (current_key[2] = key[2] - 1; current_key[2] <= key[2] + 1;
         ++current_key[2]) {
        for (current_key[1] = key[1] - 1; current_key[1] <= key[1] + 1;
             ++current_key[1]) {
            for (current_key[0] = key[0] - 1; current_key[0] <= key[0] + 1;
                 ++current_key[0]) {
                if (current_key != key) {
                    octomap::OcTreeNode* node = octree_->search(key);
                    if (node && octree_->isNodeOccupied(node)) {
                        // We have a neighbor => not a speckle!
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

void OctomapWorld::generateMarkerArray(
        const std::string& tf_frame,
        visualization_msgs::MarkerArray* occupied_nodes,
        visualization_msgs::MarkerArray* free_nodes) {
    CHECK_NOTNULL(occupied_nodes);
    CHECK_NOTNULL(free_nodes);

    // Prune the octree first.
    octree_->prune();
    int tree_depth = octree_->getTreeDepth() + 1;

    // In the marker array, assign each node to its respective depth level, since
    // all markers in a CUBE_LIST must have the same scale.
    occupied_nodes->markers.resize(tree_depth);
    free_nodes->markers.resize(tree_depth);

    // Metric min and max z of the map:
    double min_x, min_y, min_z, max_x, max_y, max_z;
    octree_->getMetricMin(min_x, min_y, min_z);
    octree_->getMetricMax(max_x, max_y, max_z);

    // Update values from params if necessary.
    if (params_.visualize_min_z > min_z) {
        min_z = params_.visualize_min_z;
    }
    if (params_.visualize_max_z < max_z) {
        max_z = params_.visualize_max_z;
    }

    for (int i = 0; i < tree_depth; ++i) {
        double size = octree_->getNodeSize(i);

        occupied_nodes->markers[i].header.frame_id = tf_frame;
        occupied_nodes->markers[i].ns = "map";
        occupied_nodes->markers[i].id = i;
        occupied_nodes->markers[i].type = visualization_msgs::Marker::CUBE_LIST;
        occupied_nodes->markers[i].scale.x = size;
        occupied_nodes->markers[i].scale.y = size;
        occupied_nodes->markers[i].scale.z = size;

        free_nodes->markers[i] = occupied_nodes->markers[i];
    }

    for (octomap::LabelOcTree::leaf_iterator it = octree_->begin_leafs(),
         end = octree_->end_leafs();
         it != end; ++it) {
        geometry_msgs::Point cube_center;
        cube_center.x = it.getX();
        cube_center.y = it.getY();
        cube_center.z = it.getZ();

        if (cube_center.z > max_z || cube_center.z < min_z) {
            continue;
        }

        int depth_level = it.getDepth();

        if (octree_->isNodeOccupied(*it)) {
            occupied_nodes->markers[depth_level].points.push_back(cube_center);
            occupied_nodes->markers[depth_level].colors.push_back
                    (getEncodedColor(it));

            //percentToColor(colorizeMapByHeight(it.getZ(), min_z, max_z)));
        } else {
            free_nodes->markers[depth_level].points.push_back(cube_center);
            free_nodes->markers[depth_level].colors.push_back(
                        percentToColor(colorizeMapByHeight(it.getZ(), min_z, max_z)));
        }
    }

    for (int i = 0; i < tree_depth; ++i) {
        if (occupied_nodes->markers[i].points.size() > 0) {
            occupied_nodes->markers[i].action = visualization_msgs::Marker::ADD;
        } else {
            occupied_nodes->markers[i].action = visualization_msgs::Marker::DELETE;
        }

        if (free_nodes->markers[i].points.size() > 0) {
            free_nodes->markers[i].action = visualization_msgs::Marker::ADD;
        } else {
            free_nodes->markers[i].action = visualization_msgs::Marker::DELETE;
        }
    }
}

std_msgs::ColorRGBA OctomapWorld::getEncodedColor(octomap::LabelOcTree::iterator it)
{
    octomap::LabelOcTreeNode& node = *it;
    octomap::LabelOcTreeNode::Label& label = node.getLabel();

    std_msgs::ColorRGBA color;
    color.a = 1;
    /*  if (label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_FLOOR)
    {
        color.r = label.r;
        color.g = label.g;
        color.b = label.b;
    }
    else if (label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_WALL)
    {
        color.r = label.r;
        color.g = label.g;
        color.b = label.b;
    }
    else if (label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_TABLE)
    {
        color.r = label.r;
        color.g = label.g;
        color.b = label.b;
    }
    else if (label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_CHAIR)
    {
        color.r = label.r;
        color.g = label.g;
        color.b = label.b;
    }
    else
    {
        color.r = label.r;
        color.g = label.g;
        color.b = label.b;
    }
*/


    if (label.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED)
    {
        color.r = 1;
        color.g = 0;
        color.b = 0;
    }
    else if (label.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_VISITED)
    {
        color.r = 0;
        color.g = 1;
        color.b = 0;
    }
    else
    {
        color.r = 0;
        color.g = 0;
        color.b = 1;
    }

    return color;
}



void OctomapWorld::convertUnknownToFree() {
    Eigen::Vector3d min_bound, max_bound;
    getMapBounds(&min_bound, &max_bound);
    convertUnknownToFree(min_bound, max_bound);
}

void OctomapWorld::convertUnknownToFree(const Eigen::Vector3d& min_bound,
                                        const Eigen::Vector3d& max_bound) {
    const bool lazy_eval = true;
    const double log_odds_value = octree_->getClampingThresMinLog();
    const double resolution = octree_->getResolution();
    const double epsilon = 0.001;  // Small offset to not hit boundary of nodes.
    Eigen::Vector3d epsilon_3d;
    epsilon_3d.setConstant(epsilon);
    octomap::point3d pmin = pointEigenToOctomap(min_bound + epsilon_3d);
    octomap::point3d pmax = pointEigenToOctomap(max_bound - epsilon_3d);
    // octree_->getUnknownLeafCenters would have been easier, but it doesn't get
    // all the unknown points for some reason
    for (float x = pmin.x() + epsilon; x < pmax.x(); x += resolution) {
        for (float y = pmin.y() + epsilon; y < pmax.y(); y += resolution) {
            for (float z = pmin.z() + epsilon; z < pmax.z(); z += resolution) {
                octomap::OcTree::NodeType* res = octree_->search(x, y, z);
                if (res == NULL) {
                    // Point is unknown, set it free
                    octree_->setNodeValue(x, y, z, log_odds_value, lazy_eval);
                }
            }
        }
    }
    if (lazy_eval) {
        // octree_->updateInnerOccupancy();
    }
    octree_->prune();
}

void OctomapWorld::inflateOccupied(const Eigen::Vector3d& safety_space) {
    // Inflate all obstacles by safety_space, such that if a collision free
    // trajectory is generated in this new space, it is guaranteed that
    // safety_space around this trajectory is collision free in the original space
    const bool lazy_eval = true;
    const double log_odds_value = octree_->getClampingThresMaxLog();
    const double resolution = octree_->getResolution();
    const double epsilon = 0.001;  // Small offset to not hit boundary of nodes.
    Eigen::Vector3d epsilon_3d;
    epsilon_3d.setConstant(epsilon);

    // Compute the maximal distance from the map's bounds where a small box has to
    // be checked explicitly for its feasibility.
    double bound_threshold = 0;
    double resolution_multiple = resolution;
    while (resolution_multiple < safety_space.minCoeff() / 4.0 - epsilon) {
        bound_threshold += resolution_multiple;
        resolution_multiple *= 2;
    }
    Eigen::Vector3d map_min_bound, map_max_bound;
    getMapBounds(&map_min_bound, &map_max_bound);

    std::vector<std::pair<Eigen::Vector3d, double>> free_boxes_vector;
    getAllFreeBoxes(&free_boxes_vector);

    Eigen::Vector3d actual_position;
    octomap::KeySet occupied_keys;
    octomap::OcTreeKey actual_key;

    for (const std::pair<Eigen::Vector3d, double>& free_box : free_boxes_vector) {
        // In case box size implicates that the whole box is infeasible (an obstacle
        // is at a diststart_keyance of at most 2 * box_size (from the center of the
        // safety_space), otherwise the pruned free box would have been bigger)
        if (free_box.second < safety_space.minCoeff() / 4.0 - epsilon) {
            Eigen::Vector3d min_bound_distance = free_box.first - map_min_bound;
            Eigen::Vector3d max_bound_distance = map_max_bound - free_box.first;
            Eigen::Vector3d bound_distance =
                    min_bound_distance.cwiseMin(max_bound_distance);
            if (bound_distance.minCoeff() > bound_threshold) {
                // It's not at the map's bounds, therefore its small size depends on a
                // near obstacle.
                getKeysBoundingBox(free_box.first,
                                   Eigen::Vector3d::Constant(free_box.second),
                                   &occupied_keys, BoundHandling::kIncludePartialBoxes);
                continue;
            }
        }

        // In case the whole box is feasible, nothing has to be done with this box
        if (getCellStatusBoundingBox(
                    free_box.first, safety_space + Eigen::Vector3d::Constant(
                        free_box.second)) != kOccupied) {
            continue;
        }

        // In case the whole box can't be feasible (bounding box of safety_space
        // around a point on one bound of the box would hit obstacle on the other
        // side)
        if (getCellStatusBoundingBox(
                    free_box.first,
                    Eigen::Vector3d::Constant(resolution - epsilon)
                    .cwiseMax(safety_space - Eigen::Vector3d::Constant(
                                  free_box.second))) == kOccupied) {
            getKeysBoundingBox(free_box.first,
                               Eigen::Vector3d::Constant(free_box.second),
                               &occupied_keys, BoundHandling::kIncludePartialBoxes);
            continue;
        }

        // Otherwise find which obstacles cause some parts of the box to be
        // infeasible, and find all those points through inflating those obstacles
        std::vector<std::pair<Eigen::Vector3d, double>> occupied_boxes_vector;
        getOccupiedBoxesBoundingBox(
                    free_box.first,
                    safety_space + Eigen::Vector3d::Constant(free_box.second),
                    &occupied_boxes_vector);
        for (const std::pair<Eigen::Vector3d, double>& box_occupied :
             occupied_boxes_vector) {
            // Infeasible volume caused by box_occupied
            Eigen::Vector3d infeasible_box_min =
                    box_occupied.first -
                    (Eigen::Vector3d::Constant(box_occupied.second) + safety_space) / 2;
            Eigen::Vector3d infeasible_box_max =
                    box_occupied.first +
                    (Eigen::Vector3d::Constant(box_occupied.second) + safety_space) / 2;
            // Volume of free_box
            Eigen::Vector3d actual_box_min =
                    free_box.first - Eigen::Vector3d::Constant(free_box.second) / 2;
            Eigen::Vector3d actual_box_max =
                    free_box.first + Eigen::Vector3d::Constant(free_box.second) / 2;
            // Overlapping volume of box_infeasible and free_box
            Eigen::Vector3d bbx_min = actual_box_min.cwiseMax(infeasible_box_min);
            Eigen::Vector3d bbx_max = actual_box_max.cwiseMin(infeasible_box_max);
            Eigen::Vector3d bbx_center = (bbx_min + bbx_max) / 2;
            Eigen::Vector3d bbx_size = bbx_max - bbx_min;
            getKeysBoundingBox(bbx_center, bbx_size, &occupied_keys,
                               BoundHandling::kIncludePartialBoxes);
        }
    }

    // Inflate all obstacles at the map's borders, since the obstacles that are
    // less than safety_space / 2 away from the boundary have to be inflated
    // beyond the boundary.
    Eigen::Vector3d direction, bounding_box_center, bounding_box_size;
    std::vector<std::pair<Eigen::Vector3d, double>> occupied_boxes_vector;
    for (unsigned i = 0u; i < 3u; i++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            direction = Eigen::Vector3d::Zero();
            direction[i] = sign;
            bounding_box_center = getMapCenter();
            bounding_box_center[i] +=
                    sign * (getMapSize()[i] - safety_space[i] / 2) / 2;
            bounding_box_size = getMapSize();
            bounding_box_size[i] = safety_space[i] / 2;
            getOccupiedBoxesBoundingBox(bounding_box_center,
                                        bounding_box_size - epsilon_3d,
                                        &occupied_boxes_vector);
            for (const std::pair<Eigen::Vector3d, double>& occupied_box :
                 occupied_boxes_vector) {
                // Set just the inflated volume that is outside the original map bounds.
                Eigen::Vector3d outer_bbx_min =
                        occupied_box.first -
                        (Eigen::Vector3d::Constant(occupied_box.second) + safety_space) / 2;
                Eigen::Vector3d outer_bbx_max =
                        occupied_box.first +
                        (Eigen::Vector3d::Constant(occupied_box.second) + safety_space) / 2;
                if (sign == -1) {
                    outer_bbx_max[i] = map_min_bound[i];
                } else {
                    outer_bbx_min[i] = map_max_bound[i];
                }
                Eigen::Vector3d outer_bbx_center = (outer_bbx_min + outer_bbx_max) / 2;
                Eigen::Vector3d outer_bbx_size =
                        outer_bbx_max - outer_bbx_min - epsilon_3d;
                getKeysBoundingBox(outer_bbx_center, outer_bbx_size, &occupied_keys,
                                   BoundHandling::kIncludePartialBoxes);
            }
        }
    }

    // Set all infeasible points occupied
    for (octomap::OcTreeKey key : occupied_keys) {
        octree_->setNodeValue(octree_->keyToCoord(key), log_odds_value, lazy_eval);
    }

    if (lazy_eval) {
        //octree_->updateInnerOccupancy();
    }
    octree_->prune();
}

void OctomapWorld::getKeysBoundingBox(
        const Eigen::Vector3d& position, const Eigen::Vector3d& bounding_box_size,
        octomap::KeySet* keys, const BoundHandling& insertion_method) const {
    const double resolution = octree_->getResolution();
    Eigen::Vector3d bbx_min, bbx_max;
    adjustBoundingBox(position, bounding_box_size, insertion_method, &bbx_min,
                      &bbx_max);
    Eigen::Vector3d actual_position;
    octomap::OcTreeKey actual_key;
    for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
         x_position += resolution) {
        for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
             y_position += resolution) {
            for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
                 z_position += resolution) {
                actual_position << x_position, y_position, z_position;
                coordToKey(actual_position, &actual_key);
                keys->insert(actual_key);
            }
        }
    }
}

void OctomapWorld::adjustBoundingBox(const Eigen::Vector3d& position,
                                     const Eigen::Vector3d& bounding_box_size,
                                     const BoundHandling& insertion_method,
                                     Eigen::Vector3d* bbx_min,
                                     Eigen::Vector3d* bbx_max) const {
    const double resolution = octree_->getResolution();
    Eigen::Vector3d resolution_3d;
    resolution_3d.setConstant(resolution);
    const double epsilon = 0.001;  // Small offset to not hit boundary of nodes.
    Eigen::Vector3d epsilon_3d;
    epsilon_3d.setConstant(epsilon);

    if (insertion_method == BoundHandling::kDefault) {
        *bbx_min = position - bounding_box_size / 2 - epsilon_3d;
        *bbx_max = position + bounding_box_size / 2 + epsilon_3d;

    } else if (insertion_method == BoundHandling::kIncludePartialBoxes) {
        // If the bbx is exactly on boundary of a node, reducing the bbx by
        // epsilon_3d will avoid to include the adjacent nodes as well.
        *bbx_min = position - bounding_box_size / 2 + epsilon_3d;
        *bbx_max = position + bounding_box_size / 2 - epsilon_3d;
        // Align positions to the center of the octree boxes
        octomap::OcTreeKey bbx_min_key =
                octree_->coordToKey(pointEigenToOctomap(*bbx_min));
        octomap::OcTreeKey bbx_max_key =
                octree_->coordToKey(pointEigenToOctomap(*bbx_max));
        *bbx_min = pointOctomapToEigen(octree_->keyToCoord(bbx_min_key));
        *bbx_max = pointOctomapToEigen(octree_->keyToCoord(bbx_max_key));
        // Add small offset so that all boxes are considered in the for-loops
        *bbx_min -= epsilon_3d;
        *bbx_max += epsilon_3d;
    } else if (insertion_method == BoundHandling::kIgnorePartialBoxes) {
        // If the bbx is exactly on boundary of a node, incrementing the bbx by
        // epsilon_3d will include that node as well.
        *bbx_min = position - bounding_box_size / 2 - epsilon_3d;
        *bbx_max = position + bounding_box_size / 2 + epsilon_3d;
        // Align positions to the center of the octree boxes
        octomap::OcTreeKey bbx_min_key =
                octree_->coordToKey(pointEigenToOctomap(*bbx_min));
        octomap::OcTreeKey bbx_max_key =
                octree_->coordToKey(pointEigenToOctomap(*bbx_max));
        *bbx_min = pointOctomapToEigen(octree_->keyToCoord(bbx_min_key));
        *bbx_max = pointOctomapToEigen(octree_->keyToCoord(bbx_max_key));
        // Crop bounding box so that it excludes the partially included boxes
        *bbx_min += resolution_3d;
        *bbx_max -= resolution_3d;
        // Add small offset so that all boxes are considered in the for-loops
        *bbx_min -= epsilon_3d;
        *bbx_max += epsilon_3d;
    }
}

double OctomapWorld::colorizeMapByHeight(double z, double min_z,
                                         double max_z) const {
    return (1.0 - std::min(std::max((z - min_z) / (max_z - min_z), 0.0), 1.0));
}

std_msgs::ColorRGBA OctomapWorld::percentToColor(double h) const {
    // Helen's note: direct copy from OctomapProvider.
    std_msgs::ColorRGBA color;
    color.a = 0.1;
    // blend over HSV-values (more colors)

    double s = 1.0;
    double v = 1.0;

    h -= floor(h);
    h *= 6;
    int i;
    double m, n, f;

    i = floor(h);
    f = h - i;
    if (!(i & 1)) f = 1 - f;  // if i is even
    m = v * (1 - s);
    n = v * (1 - s * f);

    switch (i) {
    case 6:
    case 0:
        color.r = v;
        color.g = n;
        color.b = m;
        break;
    case 1:
        color.r = n;
        color.g = v;
        color.b = m;
        break;
    case 2:
        color.r = m;
        color.g = v;
        color.b = n;
        break;
    case 3:
        color.r = m;
        color.g = n;
        color.b = v;
        break;
    case 4:
        color.r = n;
        color.g = m;
        color.b = v;
        break;
    case 5:
        color.r = v;
        color.g = m;
        color.b = n;
        break;
    default:
        color.r = 1;
        color.g = 0.5;
        color.b = 0.5;
        break;
    }

    return color;
}

Eigen::Vector3d OctomapWorld::getMapCenter() const {
    // Metric min and max z of the map:
    double min_x, min_y, min_z, max_x, max_y, max_z;
    octree_->getMetricMin(min_x, min_y, min_z);
    octree_->getMetricMax(max_x, max_y, max_z);

    Eigen::Vector3d min_3d(min_x, min_y, min_z);
    Eigen::Vector3d max_3d(max_x, max_y, max_z);

    return min_3d + (max_3d - min_3d) / 2;
}

Eigen::Vector3d OctomapWorld::getMapSize() const {
    // Metric min and max z of the map:
    double min_x, min_y, min_z, max_x, max_y, max_z;
    octree_->getMetricMin(min_x, min_y, min_z);
    octree_->getMetricMax(max_x, max_y, max_z);

    return Eigen::Vector3d(max_x - min_x, max_y - min_y, max_z - min_z);
}

void OctomapWorld::getMapBounds(Eigen::Vector3d* min_bound,
                                Eigen::Vector3d* max_bound) const {
    CHECK_NOTNULL(min_bound);
    CHECK_NOTNULL(max_bound);
    // Metric min and max z of the map:
    double min_x, min_y, min_z, max_x, max_y, max_z;
    octree_->getMetricMin(min_x, min_y, min_z);
    octree_->getMetricMax(max_x, max_y, max_z);

    *min_bound = Eigen::Vector3d(min_x, min_y, min_z);
    *max_bound = Eigen::Vector3d(max_x, max_y, max_z);
}

bool OctomapWorld::getNearestFreePoint(const Eigen::Vector3d& position,
                                       Eigen::Vector3d* free_position) const {
    const double epsilon = 1e-3;  // Small offset to not hit boundaries
    // Check if the given position is already unoccupied
    if (getCellStatusPoint(position) == CellStatus::kFree) {
        *free_position = position;
        return true;
    }

    const double resolution = octree_->getResolution();
    double bbx_size = resolution;
    std::vector<std::pair<Eigen::Vector3d, double>> free_box_vector;
    // Find the nearest free boxes, enlarge searching volume around position until
    // there is something unoccupied
    while (free_box_vector.empty() && bbx_size < getMapSize().maxCoeff()) {
        getFreeBoxesBoundingBox(position, Eigen::Vector3d::Constant(bbx_size),
                                &free_box_vector);
        bbx_size += resolution;
    }
    if (free_box_vector.empty()) {
        return false;  // There are no free boxes in the octomap
    }

    // Overestimate minimum distance between desired position and free position
    double min_distance = bbx_size;
    Eigen::Vector3d actual_distance;
    Eigen::Vector3d actual_nearest_position;
    for (const std::pair<Eigen::Vector3d, double>& free_box : free_box_vector) {
        // Distance between center of box and position
        actual_distance = position - free_box.first;
        // Limit the distance such that it is still in the box
        actual_distance = actual_distance.cwiseMin(
                    Eigen::Vector3d::Constant(free_box.second / 2 - epsilon));
        actual_distance = actual_distance.cwiseMax(
                    Eigen::Vector3d::Constant(-free_box.second / 2 + epsilon));
        // Nearest position to the desired position
        actual_nearest_position = free_box.first + actual_distance;
        // Check if this is the best position found so far
        if ((position - actual_nearest_position).norm() < min_distance) {
            min_distance = (position - actual_nearest_position).norm();
            *free_position = actual_nearest_position;
        }
    }
    return true;
}

void OctomapWorld::setRobotSize(const Eigen::Vector3d& robot_size) {
    robot_size_ = robot_size;
}

Eigen::Vector3d OctomapWorld::getRobotSize() const { return robot_size_; }

bool OctomapWorld::checkCollisionWithRobot(
        const Eigen::Vector3d& robot_position) {
    return checkSinglePoseCollision(robot_position);
}

bool OctomapWorld::checkPathForCollisionsWithRobot(
        const std::vector<Eigen::Vector3d>& robot_positions,
        size_t* collision_index) {
    // Iterate over vector of poses.
    // Check each one.
    // Return when a collision is found, and return the index of the earliest
    // collision.
    for (size_t i = 0; i < robot_positions.size(); ++i) {
        if (checkSinglePoseCollision(robot_positions[i])) {
            if (collision_index != nullptr) {
                *collision_index = i;
            }
            return true;
        }
    }
    return false;
}

bool OctomapWorld::checkSinglePoseCollision(
        const Eigen::Vector3d& robot_position) const {
    if (params_.treat_unknown_as_occupied) {
        return (CellStatus::kFree !=
                getCellStatusBoundingBox(robot_position, robot_size_));
    } else {
        return (CellStatus::kOccupied ==
                getCellStatusBoundingBox(robot_position, robot_size_));
    }
}

void OctomapWorld::getChangedPoints(
        std::vector<Eigen::Vector3d>* changed_points,
        std::vector<bool>* changed_states) {
    CHECK_NOTNULL(changed_points);
    // These keys are always *leaf node* keys, even if the actual change was in
    // a larger cube (see Octomap docs).
    octomap::KeyBoolMap::const_iterator start_key = octree_->changedKeysBegin();
    octomap::KeyBoolMap::const_iterator end_key = octree_->changedKeysEnd();

    changed_points->clear();
    if (changed_states != NULL) {
        changed_states->clear();
    }

    for (octomap::KeyBoolMap::const_iterator iter = start_key; iter != end_key;
         ++iter) {
        octomap::OcTreeNode* node = octree_->search(iter->first);
        bool occupied = octree_->isNodeOccupied(node);
        Eigen::Vector3d center =
                pointOctomapToEigen(octree_->keyToCoord(iter->first));

        changed_points->push_back(center);
        if (changed_states != NULL) {
            changed_states->push_back(occupied);
        }
    }
    octree_->resetChangeDetection();
}

void OctomapWorld::coordToKey(const Eigen::Vector3d& coord,
                              octomap::OcTreeKey* key) const {
    octomap::point3d position(coord.x(), coord.y(), coord.z());
    *key = octree_->coordToKey(position);
}

void OctomapWorld::keyToCoord(const octomap::OcTreeKey& key,
                              Eigen::Vector3d* coord) const {
    octomap::point3d position;
    position = octree_->keyToCoord(key);
    *coord << position.x(), position.y(), position.z();
}

//void OctomapWorld::updateCertaintyValue(octomap::LabelOcTreeNode * n, uint8_t val){

//    octomap::LabelOcTreeNode::Label& single_voxel = n->getLabel() ;
//    // if(single_voxel.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED)
//    //{
//    single_voxel.object_certainty = val ;
//    //}
//}

//void OctomapWorld::updateVoxelsInterest(octomap::LabelOcTreeNode * n) {
//    octomap::LabelOcTreeNode::Label& single_voxel = n->getLabel() ;
//    uint8_t cer_threshold = labelPconfig_.obj_certainty_threshold ;
//    std::cout << "THRESHOLD" << cer_threshold << std::endl  ;
//    single_voxel.type= (single_voxel.object_certainty > cer_threshold)?
//                octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_VISITED:
//                octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED;

//}
void OctomapWorld::UpdateID()
{

    //    octomap::OcTreeLUT lut(16);
    //    // OcTreeKey start_key (32768, 32768, 32768);
    //    octomap::OcTreeKey neighbor_key;

    //    for (octomap::LabelOcTree::leaf_iterator it = octree_->begin_leafs(),
    //         end = octree_->end_leafs(); it != end; ++it)
    //    {
    //        //  pcl::PointXYZ point(it.getX(), it.getY(), it.getZ());
    //        octomap::OcTreeKey start_key (it.getX(), it.getY(), it.getZ());
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::N, neighbor_key);
    //        //if(neighbor_key)
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::S, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::W, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::E, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::T, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::B, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::SE, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::SW, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::BS, neighbor_key)
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::TNW, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::BNW, neighbor_key);
    //        lut.genNeighborKey(start_key, octomap::OcTreeLUT::BSE, neighbor_key);

    //        //        if (octree_->isNodeOccupied(*it)) {
    //        //            octomap::LabelOcTreeNode& node = *it;
    //        //            octomap::LabelOcTreeNode::Label& label = node.getLabel();
    //        //            //
    //        //            // std::vector<octomap::octomap::point3d> vec ;
    //        //            if(label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_CHAIR )
    //        //            {
    //        //            }
    //        //            else
    //        //            {
    //        //            }
    //    }
    //    //        else
    //    //        {

    //    //        }

}



void OctomapWorld::updateIntrestValue()
{
    for (octomap::LabelOcTree::leaf_iterator it = octree_->begin_leafs(),
         end = octree_->end_leafs(); it != end; ++it) {

        if (octree_->isNodeOccupied(*it))
        {
            octomap::LabelOcTreeNode& node = *it;
            octomap::LabelOcTreeNode::Label& label = node.getLabel();
            if (label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_FLOOR || label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_TABLE || label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_WALL || label.object_class == octomap::LabelOcTreeNode::Label::VOXEL_NOT_LABELED)
            {
                label.type =  octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_NOT_INTEREST ;
                //label.num_of_vis += 1;
            }
            else
            {
                //label.num_of_vis += 1;
                if (label.num_of_vis > 5)
                    label.type = octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_VISITED ;
                else
                    label.type =  octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED ;
            }
        }
    }
}


double OctomapWorld::UpdateNumberofVisits(geometry_msgs::Pose p){
   // ROS_INFO("UpdateNumberofVisits");
    std::vector<std::vector<Eigen::Vector3d> > cam_bound_normals;
    double pitch = M_PI * 15 / 180.0; // conert to R
   // ROS_INFO("pitch %f",pitch);

    double camTop = (pitch - M_PI * 45/ 360.0) + M_PI / 2.0;
   // ROS_INFO("camTop %f",camTop);

    double camBottom = (pitch + M_PI * 45 / 360.0) - M_PI / 2.0;
   // ROS_INFO("camBottom %f",camBottom);

    double side = M_PI * (58) / 360.0 - M_PI / 2.0;
   // ROS_INFO("side %f",side);

    Eigen::Vector3d bottom(cos(camBottom), 0.0, -sin(camBottom));
    Eigen::Vector3d top(cos(camTop), 0.0, -sin(camTop));
    Eigen::Vector3d right(cos(side), sin(side), 0.0);
    Eigen::Vector3d left(cos(side), -sin(side), 0.0);
    Eigen::AngleAxisd m = Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitY());
    Eigen::Vector3d rightR = m * right;
    Eigen::Vector3d leftR = m * left;
    rightR.normalize();
    leftR.normalize();
    std::vector<Eigen::Vector3d> cam_bound_normals_a;
    cam_bound_normals_a.push_back(Eigen::Vector3d(bottom.x(), bottom.y(), bottom.z()));
    cam_bound_normals_a.push_back(Eigen::Vector3d(top.x(), top.y(), top.z()));
    cam_bound_normals_a.push_back(Eigen::Vector3d(rightR.x(), rightR.y(), rightR.z()));
    cam_bound_normals_a.push_back(Eigen::Vector3d(leftR.x(), leftR.y(), leftR.z()));
    cam_bound_normals.push_back(cam_bound_normals_a);
    //ROS_INFO("cam_bound_normals size %f",cam_bound_normals.size());


    Eigen::Vector3d origin(p.position.x, p.position.y, p.position.z);
    tf::Quaternion or_orientation(p.orientation.x,p.orientation.y, p.orientation.z, p.orientation.w);
    tf::Matrix3x3 m2(or_orientation);
    double yaw_p , roll_p, pitch_p;
    m2.getRPY(roll_p, pitch_p, yaw_p);
    const double disc = 0.15;
    int range = 3;
    Eigen::Vector3d vec;

    double rangeSq = pow(range, 2.0);

    for (octomap::LabelOcTree::leaf_iterator it = octree_->begin_leafs(),
         end = octree_->end_leafs(); it != end; ++it) {

        vec[0] = it.getX(); vec[1] =it.getY() ; vec[2] = it.getZ() ;
        Eigen::Vector3d dir = vec - origin;

        if (dir.transpose().dot(dir) > rangeSq) {
            continue;
        }

        bool insideAFieldOfView = false;
        // Check that voxel center is inside one of the fields of view.
        for (typename std::vector<std::vector<Eigen::Vector3d>>::iterator itCBN = cam_bound_normals.begin();
             itCBN != cam_bound_normals.end(); itCBN++) {

            bool inThisFieldOfView = true;

            for (typename std::vector<Eigen::Vector3d>::iterator itSingleCBN = itCBN->begin();
                 itSingleCBN != itCBN->end(); itSingleCBN++) {

                Eigen::Vector3d normal = Eigen::AngleAxisd(yaw_p, Eigen::Vector3d::UnitZ())
                        * (*itSingleCBN);

                double val = dir.dot(normal.normalized());
                if (val < SQRT2 * disc) {
                    inThisFieldOfView = false;
                    break;
                }
            }
            if (inThisFieldOfView) {
                insideAFieldOfView = true;
                break;
            }
        }

        if (!insideAFieldOfView) {

            continue;
        }
       //ROS_INFO("FOUND ******************************************************************");

        // only update the ones on the FOV
        octomap::LabelOcTreeNode& node = *it;
        octomap::LabelOcTreeNode::Label& label = node.getLabel();
        label.num_of_vis +=1 ;
    }
    return 0;
}


void OctomapWorld::updateSingleVoxelInfo(octomap::LabelOcTreeNode * n, int class_index , double certainty_val )
{
    //ROS_INFO("UPDATE Voxels Info");
    //std::cout << "INDEX " << class_index << " Cer" << certainty_val << std::endl << std::flush ;
    //we can fill the color as well
    //octomap::LabelOcTreeNode::Label single_voxel =node->getLabel() ;
    //std::cout << single_voxel.object_ID ;

    octomap::LabelOcTreeNode::Label& single_voxel =n->getLabel() ;
    single_voxel.object_certainty = certainty_val ;
    if (class_index == 0 )
    {
        single_voxel.object_class = octomap::LabelOcTreeNode::Label::VOXEL_FLOOR;
        single_voxel.r = 0;
        single_voxel.g = 0 ;
        single_voxel.b = 1 ;
        //single_voxel.num_of_vis += 1 ;
    }
    else if (class_index == 1)
    {
        single_voxel.object_class = octomap::LabelOcTreeNode::Label::VOXEL_WALL;
        single_voxel.r = 0;
        single_voxel.g = 1 ;
        single_voxel.b = 1 ;
        // single_voxel.num_of_vis += 1 ;
    }
    else if (class_index == 2)
    {
        single_voxel.object_class = octomap::LabelOcTreeNode::Label::VOXEL_TABLE;
        single_voxel.r = 0.666666667;
        single_voxel.g = 0.470588235;
        single_voxel.b = 0.784313725 ;
        // single_voxel.num_of_vis += 1 ;
    }
    else if (class_index == 3)
    {
        single_voxel.object_class = octomap::LabelOcTreeNode::Label::VOXEL_CHAIR;
        single_voxel.r = 1;
        single_voxel.g = 0 ;
        single_voxel.b = 0 ;
        // single_voxel.num_of_vis += 1 ;
    }
    else
    {
        single_voxel.object_class = octomap::LabelOcTreeNode::Label::VOXEL_NOT_LABELED;
        single_voxel.r = 0.196078431;
        single_voxel.g = 0.196078431 ;
        single_voxel.b = 0.196078431 ;
        //  single_voxel.num_of_vis += 1 ;
    }


}

int OctomapWorld::identifyClass(octomap::point3d point_senmantic_clolor) {
    //  'floor':	[0,0,255],
    //  'wall':	       [0,255,255],
    //  'table':       [170,120,200],
    //  'chair':       [255,0,0],
    // 'clutter':     [50,50,50]}
    int class_type = -1;
    if (point_senmantic_clolor(0) == 0 && point_senmantic_clolor(1) == 0 && point_senmantic_clolor(2) == 255)
    {
        //std::cout << "Floor" << std::endl << std::flush;
        class_type =0 ;
    }
    else if (point_senmantic_clolor(0) == 0 && point_senmantic_clolor(1) == 255 && point_senmantic_clolor(2) == 255)
    {
        //std::cout << "wall" << std::endl << std::flush;
        class_type =1 ;
    }
    else if (point_senmantic_clolor(0) == 170 && (point_senmantic_clolor(1) >= 119 && point_senmantic_clolor(1) <= 120) &&  point_senmantic_clolor(2) == 200)
    {
       // std::cout << "table" << std::endl << std::flush;
        class_type =2 ;
    }
    else if (point_senmantic_clolor(0) == 255 && point_senmantic_clolor(1) == 0 && point_senmantic_clolor(2) == 0)
    {
        //std::cout << "chair" << std::endl << std::flush;
        class_type =3 ;
    }
    else
    {
        // std::cout << "clutter" << std::endl << std::flush;
        class_type =4 ;
    }
    return class_type ;
}

double OctomapWorld::introduceNoise(int class_type_1 , int &class_index) {
    //ROS_INFO("INRODUCE NOISE");
    std::vector<double> certinity_vec ={0,0,0,0,0} ;
    certinity_vec[class_type_1] =1 ;
    // for debugging
    class_index = class_type_1 ;
    double class_certainty = 1 ;
    return class_certainty ;
}

// Not used
OctomapWorld::CellStatus OctomapWorld::getCellIneterestGainPoint(
        const Eigen::Vector3d& point, double* gain) const {
    *gain = 0.0 ;
    octomap::LabelOcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
    if (node == NULL) {
        return CellStatus::kUnknown;
    } else {
        if (octree_->isNodeOccupied(node)) {
            octomap::LabelOcTreeNode::Label& label = node->getLabel() ;
            if(label.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED)
                *gain = 0.5 ;
            return CellStatus::kOccupied;
        } else {
            return CellStatus::kFree;
        }
    }
}

//Used
int OctomapWorld::getCellIneterestCellType(double x, double y, double z) const {

    octomap::LabelOcTreeNode* node = octree_->search(x, y, z);
    if (node == NULL) {
        return 1;
    } else {
        if (octree_->isNodeOccupied(node)) {
            octomap::LabelOcTreeNode::Label& label = node->getLabel() ;
            if(label.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED)
                return 2; 
            else if (label.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_VISITED)
                return 3; 
            else 
                return 4; 
        } else {
            return 0;
        }
    }
}


// Used
double OctomapWorld::getCellIneterestGain(
        const Eigen::Vector3d& point) const {
    octomap::LabelOcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
    if (node == NULL) {
        return 0.5;
    }
    else {
        if (octree_->isNodeOccupied(node)) {
            octomap::LabelOcTreeNode::Label& label = node->getLabel() ;
            if(label.type == octomap::LabelOcTreeNode::Label::VOXEL_OCCUPIED_INTEREST_NOT_VISITED)
                return 0.5  ;
            else
                return 0.8;
        }
        else {
            return 0.2;
        }
    }
}


}  // namespace volumetric_mapping

