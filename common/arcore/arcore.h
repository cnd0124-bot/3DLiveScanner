#ifndef ARCORE_ARCORE_H
#define ARCORE_ARCORE_H

#include <map>
#include <jni.h>
#include <arcore/camera.h>
#include <data/image.h>
#include <data/mesh.h>
#include <gl/renderer.h>

namespace oc {

    struct DetectedPlane {
        int type; // 0: floor (horizontal up), 1: ceiling (horizontal down), 2: wall (vertical)
        glm::vec3 center;
        glm::vec3 normal;
        std::vector<glm::vec3> polygon;
        float extentX;
        float extentZ;
    };

    class ARCore {
    public:
        ARCore(void *env, void *context, bool faceMode, bool depthCamera = false);

        ~ARCore();

        void Clear(bool detach);

        void OnPause();

        void OnResume();

        void OnDisplayGeometryChanged(int display_rotation, int width, int height);

        void Configure(void* session, void* frame);

        float CountFrameError();

        bool Process(bool update = true);

        std::vector<glm::vec3> GetActiveAnchors();

        std::vector<float> GetDistortion();

        glm::vec3 HitTest(int x, int y);

        Mesh GetFace(glm::mat4 matrix) { UpdateFace(matrix); return face_mesh; };

        std::vector<DetectedPlane> GetPlanes();

        std::vector<glm::vec4> GetPointCloud() { UpdateFeaturePoints(); return points; }

        glm::mat4 GetProjection() { return projection_mat; }

        glm::mat4 GetView() { return view_mat; }

        bool HasCoordinateSystem() { return has_coordinate_system_; }

        void RemoveFaceDetails() { face_not_all = true; }

        void RenderCamera(ARCoreCamera::Effect effect = ARCoreCamera::GRAYSCALE, int scale = 1);

        void SetNVScheme(ARCoreCamera::NightVisionScheme s) { camera.SetNVScheme(s); }

        void SetOffset(float value) { offset = value; }

        void SetResolution(float res) { resolution = res; }

        Image* GetDepthMap(bool confidence, bool increasing, int s = 1);

        // diagnostics
        int dbg_session_status = -999;
        int dbg_config_status = -999;
        int dbg_update_status = -999;
        int dbg_tracking_state = -1;
        int dbg_failure_reason = 0;
        int dbg_feature_points = 0;
        int dbg_depth_points = 0;
        int dbg_planes = 0;
        bool UsesDepth() { return useDepth; }
        bool HasSession() { return ar_session_ != nullptr; }

    private:
        glm::mat4 GetMatrix(ArPose* ar_pose);

        glm::mat4 GetZeroTransform();

        glm::vec4 ToPoint(glm::dmat4& screen2world, double& len,
                int32_t& depthWidth, int32_t& depthHeight, int& x, int& y, double& depth);

        bool UpdateAnchor();

        void UpdateFace(glm::mat4 matrix);

        void UpdateFeaturePoints();

        std::map<id3d, ArAnchor*> ar_anchor_list;
        ArSession *ar_session_ = nullptr;
        ArFrame *ar_frame_ = nullptr;
        std::pair<id3d, ArAnchor*> ar_zero_;
        Mesh face_mesh;

        ARCoreCamera camera;

        glm::mat4 view_mat;
        glm::mat4 projection_mat;

        bool face_mode_;
        bool has_coordinate_system_;
        bool has_depth_sensor;
        std::vector<glm::vec4> points;
        float offset;
        float resolution;
        bool face_not_all = false;
        bool texture_initialized_ = false;
        bool useDepth;
        bool useDepthRaw;
        int viewportWidth;
        int viewportHeight;
        int64_t lastDepthTimestamp;
    };
}

#endif
