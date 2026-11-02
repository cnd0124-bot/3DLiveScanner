#include <arcore/arcore.h>
#include <mutex>
#include "service.h"

namespace oc {

    ARCore::ARCore(void *env, void *context, bool faceMode, bool depthCamera) {
        ar_session_ = nullptr;
        ar_frame_ = nullptr;
        useDepthRaw = false;
#ifndef ARCORE_BACKPORT
        if (env && context) {
            ArStatus status = AR_ERROR_FATAL;
            if (faceMode) {
                ArSessionFeature features[2] = {AR_SESSION_FEATURE_FRONT_CAMERA, AR_SESSION_FEATURE_END_OF_LIST};
                status = ArSession_createWithFeatures(env, context, features, &ar_session_);
            } else if (!depthCamera) {
                ArSessionFeature features[2] = {AR_SESSION_FEATURE_DO_NOT_USE_ACTIVE_DEPTH_SENSOR, AR_SESSION_FEATURE_END_OF_LIST};
                status = ArSession_createWithFeatures(env, context, features, &ar_session_);
            }
            if (status != AR_SUCCESS || !ar_session_) {
                status = ArSession_create(env, context, &ar_session_);
            }
            if (status != AR_SUCCESS || !ar_session_) {
                LOGI("ARCore ArSession_create failed: %d", status);
                return;
            }

            int rawSupported = 0;
            int autoSupported = 0;
            ArSession_isDepthModeSupported(ar_session_, AR_DEPTH_MODE_RAW_DEPTH_ONLY, &rawSupported);
            ArSession_isDepthModeSupported(ar_session_, AR_DEPTH_MODE_AUTOMATIC, &autoSupported);

            ArConfig *ar_config = nullptr;
            ArConfig_create(ar_session_, &ar_config);
            ArConfig_setFocusMode(ar_session_, ar_config, AR_FOCUS_MODE_AUTO);
            ArConfig_setPlaneFindingMode(ar_session_, ar_config, AR_PLANE_FINDING_MODE_HORIZONTAL_AND_VERTICAL);
            if (faceMode) {
                ArConfig_setAugmentedFaceMode(ar_session_, ar_config, AR_AUGMENTED_FACE_MODE_MESH3D);
                useDepth = false;
                useDepthRaw = false;
            } else {
                if (depthCamera && rawSupported) {
                    ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_RAW_DEPTH_ONLY);
                    useDepth = true;
                    useDepthRaw = true;
                } else if (autoSupported) {
                    ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_AUTOMATIC);
                    useDepth = true;
                    useDepthRaw = false;
                } else if (rawSupported) {
                    ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_RAW_DEPTH_ONLY);
                    useDepth = true;
                    useDepthRaw = true;
                } else {
                    int alwaysSupported = 0;
                    ArSession_isDepthModeSupported(ar_session_, AR_DEPTH_MODE_ALWAYS_ENABLED, &alwaysSupported);
                    if (alwaysSupported) {
                        ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_ALWAYS_ENABLED);
                        useDepth = true;
                        useDepthRaw = false;
                    } else {
                        ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_DISABLED);
                        useDepth = false;
                        useDepthRaw = false;
                    }
                }
            }
            ArConfig_setUpdateMode(ar_session_, ar_config, AR_UPDATE_MODE_BLOCKING);
            ArStatus configStatus = ArSession_configure(ar_session_, ar_config);
            if (configStatus != AR_SUCCESS) {
                LOGI("ArSession_configure failed: %d, trying fallback config", configStatus);
                ArConfig_setFocusMode(ar_session_, ar_config, AR_FOCUS_MODE_FIXED);
                ArConfig_setDepthMode(ar_session_, ar_config, AR_DEPTH_MODE_DISABLED);
                useDepth = false;
                useDepthRaw = false;
                configStatus = ArSession_configure(ar_session_, ar_config);
                LOGI("Fallback ArSession_configure status: %d", configStatus);
            }
            ArConfig_destroy(ar_config);
            ArFrame_create(ar_session_, &ar_frame_);
        } else
#endif
        {
            useDepth = depthCamera;
            useDepthRaw = depthCamera;
        }

        ar_zero_.second = 0;
        has_coordinate_system_ = false;
        has_depth_sensor = depthCamera;
        lastDepthTimestamp = 0;
        offset = 0;
        resolution = 0;
        face_mode_ = faceMode;
    }

    ARCore::~ARCore() {
        if (ar_session_) {
            ArSession_destroy(ar_session_);
            ar_session_ = nullptr;
        }
        if (ar_frame_) {
            ArFrame_destroy(ar_frame_);
            ar_frame_ = nullptr;
        }
    }

    void ARCore::Clear(bool detach) {
        if (!ar_session_) return;
        if (detach) {
            for (auto& anchor : ar_anchor_list) {
                ArAnchor_detach(ar_session_, ar_anchor_list[anchor.first]);
                ArAnchor_release(ar_anchor_list[anchor.first]);
            }
            if (ar_zero_.second) {
                ArAnchor_detach(ar_session_, ar_zero_.second);
                ArAnchor_release(ar_zero_.second);
            }
        }
        ar_anchor_list.clear();
        ar_zero_.second = 0;
        has_coordinate_system_ = false;
    }

    void ARCore::OnPause() {
        if (ar_session_) ArSession_pause(ar_session_);
    }

    void ARCore::OnResume() {
        if (ar_session_) ArSession_resume(ar_session_);
        camera.InitializeGlContent();
        texture_initialized_ = false;
    }

    void ARCore::OnDisplayGeometryChanged(int display_rotation, int width, int height) {
        viewportWidth = width;
        viewportHeight = height;
        if (ar_session_) ArSession_setDisplayGeometry(ar_session_, display_rotation, width, height);
    }

    void ARCore::Configure(void *session, void *frame) {
        ar_session_ = static_cast<ArSession *>(session);
        ar_frame_ = static_cast<ArFrame *>(frame);

        int out_is_supported = 0;
        ArSession_isDepthModeSupported(ar_session_, AR_DEPTH_MODE_AUTOMATIC, &out_is_supported);
        if (out_is_supported != 0) {
            useDepth = true;
        }
    }

    float ARCore::CountFrameError() {
        int size = 0;
        float error = 10000;
        float data[7] = {0, 0, 0, 1, 0, 0, 0};
        ArPose *ar_pose;
        ArPose_create(ar_session_, data, &ar_pose);
        glm::mat4 matrix = projection_mat * view_mat;

        ArHitResult* hit = 0;
        ArHitResultList* hits = 0;
        ArHitResult_create(ar_session_, &hit);
        ArHitResultList_create(ar_session_, &hits);
        for (glm::vec3& v : GetActiveAnchors()) {
            glm::vec4 point = matrix * glm::vec4(v, 1.0);
            point /= fabs(point.z * point.w);
            point = 0.5f * point + 0.5f;

            ArFrame_hitTest(ar_session_, ar_frame_, viewportWidth * point.x, viewportHeight * point.y, hits);
            ArHitResultList_getSize(ar_session_, hits, &size);
            for (int i = 0; i < size; i++) {
                ArTrackable *trackable = 0;
                ArHitResultList_getItem(ar_session_, hits, i, hit);
                ArHitResult_acquireTrackable(ar_session_, hit, &trackable);
                ArPoint_getPose(ar_session_, ArAsPoint(trackable), ar_pose);
                ArPose_getPoseRaw(ar_session_, ar_pose, data);
                glm::vec3 position = glm::vec3(data[4], data[5], data[6]);
                float dst = glm::distance(v, position);
                if (dst > 0) {
                    error = glm::min(error, dst);
                }
                ArTrackable_release(trackable);
            }
        }

        ArHitResultList_destroy(hits);
        ArHitResult_destroy(hit);
        ArPose_destroy(ar_pose);
        return error;
    }

    bool ARCore::Process(bool update) {
        if (!ar_session_ || !ar_frame_) return false;
        if (update) {
            if (!texture_initialized_) {
                ArSession_setCameraTextureName(ar_session_, camera.GetTextureName());
                texture_initialized_ = true;
            }
            if (ArSession_update(ar_session_, ar_frame_) != AR_SUCCESS)
                return false;
        }

        ArCamera *ar_camera;
        ArFrame_acquireCamera(ar_session_, ar_frame_, &ar_camera);
        ArCamera_getViewMatrix(ar_session_, ar_camera, glm::value_ptr(view_mat));
        ArCamera_getProjectionMatrix(ar_session_, ar_camera, 0.001f, 100.f,
                                       glm::value_ptr(projection_mat));

        ArTrackingState state = AR_TRACKING_STATE_STOPPED;
        if (!face_mode_) {
            ArCamera_getTrackingState(ar_session_, ar_camera, &state);
            if (state == AR_TRACKING_STATE_STOPPED) {
                ArCamera_release(ar_camera);
                return false;
            }
        }

        if (!face_mode_ && ar_zero_.second == 0 && state == AR_TRACKING_STATE_TRACKING) {
            ArPose *cam_pose = nullptr;
            float default_pose[7] = {0, 0, 0, 1, 0, 0, 0};
            ArPose_create(ar_session_, default_pose, &cam_pose);
            ArCamera_getPose(ar_session_, ar_camera, cam_pose);
            ArAnchor *anchor = nullptr;
            if (ArSession_acquireNewAnchor(ar_session_, cam_pose, &anchor) == AR_SUCCESS) {
                ar_zero_.first.matrix = GetMatrix(cam_pose);
                ar_zero_.second = anchor;
            }
            ArPose_destroy(cam_pose);
        }

        view_mat = view_mat * GetZeroTransform();
        ArCamera_release(ar_camera);

        if (!face_mode_) {
            UpdateAnchor();
        }
        return true;
    }

    void ARCore::RenderCamera(ARCoreCamera::Effect effect, int scale) {
        if (effect >= ARCoreCamera::DEPTH) {
            Image* img = 0;
            if (effect == ARCoreCamera::NIGHTVISION)
                img = GetDepthMap(true, false, scale);
            else if (effect == ARCoreCamera::DEPTH_INV)
                img = GetDepthMap(false, false, scale);
            else
                img = GetDepthMap(false, true, scale);
            if (img) {
                GLuint texture = GLSL::Image2GLTexture(img);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
                camera.GetShader()->Bind();
                camera.GetShader()->UniformInt("depth", 0);
                camera.DrawARCore(ar_session_, ar_frame_, effect, viewportWidth, viewportHeight);
                glDeleteTextures(1, &texture);
                delete img;
            }
        } else {
            camera.DrawARCore(ar_session_, ar_frame_, effect, viewportWidth, viewportHeight);
        }
    }

    std::vector<glm::vec3> ARCore::GetActiveAnchors() {
        float data[7] = {0, 0, 0, 1, 0, 0, 0};
        ArPose *ar_pose;
        ArPose_create(ar_session_, data, &ar_pose);
        std::vector<glm::vec3> output;

        glm::mat4 zero = glm::inverse(GetZeroTransform());
        for (auto& anchor : ar_anchor_list) {
            ArTrackingState state = AR_TRACKING_STATE_STOPPED;
            ArAnchor_getTrackingState(ar_session_, anchor.second, &state);
            if (state == AR_TRACKING_STATE_TRACKING) {
                ArAnchor_getPose(ar_session_, anchor.second, ar_pose);
                ArPose_getPoseRaw(ar_session_, ar_pose, data);
                glm::vec3 v = glm::vec3(data[4], data[5], data[6]);
                glm::vec4 p = zero * glm::vec4(v, 1.0f);
                v = p / fabs(p.w);
                output.push_back(v);
            }
        }
        ArPose_destroy(ar_pose);
        return output;
    }

    std::vector<DetectedPlane> ARCore::GetPlanes() {
        std::vector<DetectedPlane> result;
#ifndef ARCORE_BACKPORT
        if (!ar_session_) return result;
        ArTrackableList* plane_list = nullptr;
        ArTrackableList_create(ar_session_, &plane_list);
        ArSession_getAllTrackables(ar_session_, AR_TRACKABLE_PLANE, plane_list);
        int32_t plane_list_size = 0;
        ArTrackableList_getSize(ar_session_, plane_list, &plane_list_size);

        glm::mat4 zero = glm::inverse(GetZeroTransform());

        for (int32_t i = 0; i < plane_list_size; i++) {
            ArTrackable* trackable = nullptr;
            ArTrackableList_getItem(ar_session_, plane_list, i, &trackable);
            ArPlane* ar_plane = ArAsPlane(trackable);

            ArPlane* subsumed_by = nullptr;
            ArPlane_acquireSubsumedBy(ar_session_, ar_plane, &subsumed_by);
            if (subsumed_by != nullptr) {
                ArTrackable_release(ArAsTrackable(subsumed_by));
                ArTrackable_release(trackable);
                continue;
            }

            ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
            ArTrackable_getTrackingState(ar_session_, trackable, &tracking_state);
            if (tracking_state != AR_TRACKING_STATE_TRACKING) {
                ArTrackable_release(trackable);
                continue;
            }

            ArPlaneType plane_type;
            ArPlane_getType(ar_session_, ar_plane, &plane_type);

            float default_pose[7] = {0, 0, 0, 1, 0, 0, 0};
            ArPose* plane_pose = nullptr;
            ArPose_create(ar_session_, default_pose, &plane_pose);
            ArPlane_getCenterPose(ar_session_, ar_plane, plane_pose);
            glm::mat4 plane_matrix = GetMatrix(plane_pose);
            ArPose_destroy(plane_pose);

            float extent_x = 0.0f, extent_z = 0.0f;
            ArPlane_getExtentX(ar_session_, ar_plane, &extent_x);
            ArPlane_getExtentZ(ar_session_, ar_plane, &extent_z);

            int32_t polygon_size = 0;
            ArPlane_getPolygonSize(ar_session_, ar_plane, &polygon_size);
            if (polygon_size < 6) {
                ArTrackable_release(trackable);
                continue;
            }

            std::vector<float> polygon_xz(polygon_size);
            ArPlane_getPolygon(ar_session_, ar_plane, polygon_xz.data());

            glm::mat4 model_to_zero = zero * plane_matrix;
            DetectedPlane dp;
            dp.type = (int)plane_type;
            glm::vec4 center4 = model_to_zero * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            dp.center = glm::vec3(center4 / center4.w);
            glm::vec4 normal4 = model_to_zero * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            dp.normal = glm::normalize(glm::vec3(normal4));
            dp.extentX = extent_x;
            dp.extentZ = extent_z;

            for (int32_t j = 0; j < polygon_size / 2; j++) {
                glm::vec4 local_pt(polygon_xz[j * 2 + 0], 0.0f, polygon_xz[j * 2 + 1], 1.0f);
                glm::vec4 world_pt = model_to_zero * local_pt;
                dp.polygon.push_back(glm::vec3(world_pt / world_pt.w));
            }
            result.push_back(dp);
            ArTrackable_release(trackable);
        }
        ArTrackableList_destroy(plane_list);
#endif
        return result;
    }

    std::vector<float> ARCore::GetDistortion() {
        std::vector<float> output;
        for (int i = 0; i < 3; i++) {
            output.push_back(0);
        }
        return output;
    }

    glm::vec3 ARCore::HitTest(int x, int y) {
        float data[7] = {0, 0, 0, 1, 0, 0, 0};
        ArPose *ar_pose;
        ArPose_create(ar_session_, data, &ar_pose);

        int size = 0;
        ArHitResult* hit = 0;
        ArHitResultList* hits = 0;
        ArHitResult_create(ar_session_, &hit);
        ArHitResultList_create(ar_session_, &hits);
        ArFrame_hitTest(ar_session_, ar_frame_, x, y, hits);
        ArHitResultList_getSize(ar_session_, hits, &size);
        if (size > 0) {
            ArHitResultList_getItem(ar_session_, hits, 0, hit);
            ArHitResult_getHitPose(ar_session_, hit, ar_pose);
            ArPose_getPoseRaw(ar_session_, ar_pose, data);
            ArHitResultList_destroy(hits);
            ArHitResult_destroy(hit);
            ArPose_destroy(ar_pose);
            return glm::vec3(data[4], data[5], data[6]);
        }
        ArHitResultList_destroy(hits);
        ArHitResult_destroy(hit);
        ArPose_destroy(ar_pose);
        return glm::vec3(INT_MAX);
    }

    Image* ARCore::GetDepthMap(bool confidence, bool increasing, int s) {

        if (useDepth) {
            ArImage* image = 0;
            int result = 0;
            if (useDepthRaw) {
                result = ArFrame_acquireRawDepthImage16Bits(ar_session_, ar_frame_, &image);
            } else {
                result = ArFrame_acquireDepthImage16Bits(ar_session_, ar_frame_, &image);
            }

            if (result == AR_SUCCESS) {

                uint8_t* confidenceData;
                ArImage* confidenceImage = 0;
                bool hasConfidence = false;
                if (useDepthRaw) {
                    result = ArFrame_acquireRawDepthConfidenceImage(ar_session_, ar_frame_, &confidenceImage);;
                    if (result == AR_SUCCESS) {
                        int32_t dataLength;
                        ArImage_getPlaneData(ar_session_, confidenceImage, 0, (const uint8_t**)&confidenceData, &dataLength);
                        hasConfidence = true;
                    }
                }

                //get depth data
                uint16_t* imgData;
                int32_t dataLength;
                int32_t depthWidth, depthHeight, stride;
                ArImage_getPlaneRowStride(ar_session_, image, 0, &stride);
                ArImage_getPlaneData(ar_session_, image, 0, (const uint8_t**)&imgData, &dataLength);
                ArImage_getWidth(ar_session_, image, &depthWidth);
                ArImage_getHeight(ar_session_, image, &depthHeight);

                if (depthWidth > 240) s *= depthWidth / 240;
                depthWidth /= s;
                depthHeight /= s;
                Image* output = new Image(depthWidth, depthHeight);
                for (int y = 0; y < depthHeight; y++) {
                    for (int x = 0; x < depthWidth; x++) {
                        int depth = static_cast<int>((imgData[s * y * stride / 2 + s * x] & 0xFFFF) * 0.001 * 255);
                        if (!increasing && depth > 0) depth = 768 - depth;
                        output->GetData()[(y * depthWidth + x) * 4 + 0] = camera.Convert(depth, 0);
                        output->GetData()[(y * depthWidth + x) * 4 + 1] = camera.Convert(depth, 1);
                        output->GetData()[(y * depthWidth + x) * 4 + 2] = camera.Convert(depth, 2);
                        output->GetData()[(y * depthWidth + x) * 4 + 3] = 255;
                        if (confidence && hasConfidence) {
                            output->GetData()[(y * depthWidth + x) * 4 + 3] = 128 + confidenceData[s * y * depthWidth + s * x] / 2;
                        }
                    }
                }

                if (hasConfidence) {
                    ArImage_release(confidenceImage);
                }
                ArImage_release(image);
                return output;
            }
        }
        return 0;
    }

    glm::mat4 ARCore::GetMatrix(ArPose* ar_pose) {
        float* matrix = new float[16];
        ArPose_getMatrix(ar_session_, ar_pose, matrix);

        glm::mat4 output(1);
        output[0][0] = matrix[0];
        output[0][1] = matrix[1];
        output[0][2] = matrix[2];
        output[0][3] = matrix[3];
        output[1][0] = matrix[4];
        output[1][1] = matrix[5];
        output[1][2] = matrix[6];
        output[1][3] = matrix[7];
        output[2][0] = matrix[8];
        output[2][1] = matrix[9];
        output[2][2] = matrix[10];
        output[2][3] = matrix[11];
        output[3][0] = matrix[12];
        output[3][1] = matrix[13];
        output[3][2] = matrix[14];
        output[3][3] = matrix[15];
        delete[] matrix;
        return output;
    }

    glm::mat4 ARCore::GetZeroTransform() {
#ifndef ARCORE_BACKPORT
        if (ar_zero_.second) {
            ArTrackingState state = AR_TRACKING_STATE_STOPPED;
            ArAnchor_getTrackingState(ar_session_, ar_zero_.second, &state);
            if (state == AR_TRACKING_STATE_TRACKING) {
                float data[7] = {0, 0, 0, 1, 0, 0, 0};

                ArPose *ar_pose;
                ArPose_create(ar_session_, data, &ar_pose);
                ArAnchor_getPose(ar_session_, ar_zero_.second, ar_pose);
                glm::mat4 matrix = GetMatrix(ar_pose);
                ArPose_destroy(ar_pose);

                return matrix * glm::inverse(ar_zero_.first.matrix);
            }
        }
#endif
        return glm::mat4(1);
    }

    glm::vec4 ARCore::ToPoint(glm::dmat4& screen2world, double& len,
                      int32_t& depthWidth, int32_t& depthHeight, int& x, int& y, double& depth) {

        //convert sensor coordinates to screen coordinates
        glm::dvec2 T = camera.Transform(x, y, depthWidth, depthHeight);

        //create a ray from screen space to world space
        glm::dvec4 point0 = screen2world * glm::vec4(T, 0, 1);
        point0 /= glm::abs(point0.w);
        point0.w = 1;
        glm::dvec4 point1 = screen2world * glm::vec4(T, 1, 1);
        point1 /= glm::abs(point1.w);
        point1.w = 1;

        //get a point on ray that match the depth
        return point0 + (point1 - point0) / len * depth;
    }

    bool ARCore::UpdateAnchor() {
        if (!ar_session_ || !ar_frame_) return false;
        if (!ar_anchor_list.empty() && GetActiveAnchors().empty()) {
            return false;
        }

        float data[7] = {0, 0, 0, 1, 0, 0, 0};
        ArPose *ar_pose;
        ArPose_create(ar_session_, data, &ar_pose);

        bool valid = false;
        ArAnchor* ar_anchor_ = 0;
        int size = 0;
        ArHitResult* hit = 0;
        ArHitResultList* hits = 0;
        ArHitResult_create(ar_session_, &hit);
        ArHitResultList_create(ar_session_, &hits);
        for (float x = 0.25f; x <= 0.75f; x += 0.5f) {
            for (float y = 0.25f; y <= 0.75f; y += 0.5f) {
                ArFrame_hitTest(ar_session_, ar_frame_, viewportWidth * x, viewportHeight * y, hits);
                ArHitResultList_getSize(ar_session_, hits, &size);
                for (int i = 0; i < size; i++) {
                    ArTrackable* trackable = 0;
                    ArHitResultList_getItem(ar_session_, hits, i, hit);
                    ArHitResult_acquireTrackable(ar_session_, hit, &trackable);
                    ArTrackableType type = AR_TRACKABLE_NOT_VALID;
                    ArTrackable_getType(ar_session_, trackable, &type);
                    if (type == AR_TRACKABLE_POINT || type == AR_TRACKABLE_PLANE) {

                        ArHitResult_getHitPose(ar_session_, hit, ar_pose);
                        ArPose_getPoseRaw(ar_session_, ar_pose, data);
                        glm::vec3 position(data[4], data[5], data[6]);

                        id3d pos;
                        pos.matrix = GetMatrix(ar_pose);
                        float density = ANCHOR_DENSITY_BASE;
                        for (pos.layer = 0; pos.layer < ANCHOR_LAYERS; pos.layer++) {
                            pos.x = static_cast<int>(position.x / density);
                            pos.y = static_cast<int>(position.y / density);
                            pos.z = static_cast<int>(position.z / density);
                            valid = true;
                            if (ar_anchor_list.find(pos) == ar_anchor_list.end()) {
                                ArStatus ret = ArTrackable_acquireNewAnchor(ar_session_, trackable, ar_pose, &ar_anchor_);
                                if (ret == AR_SUCCESS) {
                                    if (ar_zero_.second == 0) {
                                        ar_zero_.first = pos;
                                        ar_zero_.second = ar_anchor_;
                                        break;
                                    }

                                    ar_anchor_list[pos] = ar_anchor_;
                                    while (true) {
                                        int count = 0;
                                        id3d far = pos;
                                        for (auto& anchor : ar_anchor_list) {
                                            if (anchor.first.layer == pos.layer) {
                                                if (Diff(anchor.first, pos) > Diff(far, pos)) {
                                                    far = anchor.first;
                                                }
                                                count++;
                                            }
                                        }
                                        if (count > ANCHOR_CACHE) {
                                            ArAnchor_detach(ar_session_, ar_anchor_list[far]);
                                            ArAnchor_release(ar_anchor_list[far]);
                                            ar_anchor_list.erase(far);
                                        } else {
                                            break;
                                        }
                                    }
                                }
                            }
                            density *= ANCHOR_DENSITY_SCALE;
                        }
                    }
                    ArTrackable_release(trackable);
                }
            }
        }
        ArHitResultList_destroy(hits);
        ArHitResult_destroy(hit);
        ArPose_destroy(ar_pose);

        if (!valid && !GetActiveAnchors().empty()) {
            valid = true;
        }
        return valid;
    }

    void ARCore::UpdateFace(glm::mat4 matrix) {
#ifndef ARCORE_BACKPORT
        face_mesh.vertices.clear();
        face_mesh.normals.clear();
        face_mesh.uv.clear();
        face_mesh.indices.clear();
        points.clear();
        int32_t size = 0;
        ArTrackableList* faces = 0;
        ArTrackableList_create(ar_session_, &faces);
        ArSession_getAllTrackables(ar_session_, AR_TRACKABLE_FACE, faces);
        ArTrackableList_getSize(ar_session_, faces, &size);
        for (int32_t i = 0; i < size; i++) {
            int32_t count = 0;
            const float* vertices = 0;
            const float* normals = 0;
            ArTrackable* face = 0;
            ArTrackableList_acquireItem(ar_session_, faces, i, &face);
            ArAugmentedFace_getMeshVertices(ar_session_, ArAsFace(face), &vertices, &count);
            ArAugmentedFace_getMeshNormals(ar_session_, ArAsFace(face), &normals, &count);

            float data[7] = {0, 0, 0, 1, 0, 0, 0};
            ArPose *ar_pose;
            ArPose_create(ar_session_, data, &ar_pose);
            ArAugmentedFace_getCenterPose(ar_session_, ArAsFace(face), ar_pose);
            ArPose_getPoseRaw(ar_session_, ar_pose, data);

            GLCamera pose;
            pose.position = glm::vec3(data[4], data[5], data[6]);
            pose.rotation = glm::quat(data[3], data[0], data[1], data[2]);
            pose.scale = glm::vec3(1);
            glm::mat4 transform = pose.GetTransformation();
            for (int j = 0; j < count; j++) {
                glm::vec4 point = glm::vec4(vertices[j * 3 + 0],
                                            vertices[j * 3 + 1],
                                            vertices[j * 3 + 2],
                                            1.0f);
                point = transform * point;
                point /= fabs(point.w);
                point.w = 1.0f;
                face_mesh.vertices.push_back(point);
                face_mesh.normals.push_back(glm::vec3(normals[j * 3 + 0],
                                                      normals[j * 3 + 1],
                                                      normals[j * 3 + 2]));

                point = matrix * point;
                point /= fabs(point.w);
                face_mesh.uv.push_back(0.5f * glm::vec2(point.x, point.y) + 0.5f);
            }

            const uint16_t* indices = 0;
            int32_t triangles = 0;
            ArAugmentedFace_getMeshTriangleIndices(ar_session_, ArAsFace(face), &indices, &triangles);
            for (int j = 0; j < triangles; j++) {
                if (face_not_all) {
                    bool ok = true;
                    for (int l = j * 3 + 0; l < j * 3 + 3; l++)
                    {
                        if ((indices[l] == 13) || (indices[l] == 14))
                            ok = false;
                        if ((indices[l] == 78) || (indices[l] == 95))
                            ok = false;
                        if ((indices[l] >= 80) && (indices[l] <= 82))
                            ok = false;
                        if ((indices[l] == 87) || (indices[l] == 88))
                            ok = false;
                        if ((indices[l] == 178) || (indices[l] == 191))
                            ok = false;
                        if ((indices[l] == 308) || (indices[l] == 324))
                            ok = false;
                        if ((indices[l] >= 310) && (indices[l] <= 312))
                            ok = false;
                        if ((indices[l] == 317) || (indices[l] == 318))
                            ok = false;
                        if ((indices[l] == 402) || (indices[l] == 415))
                            ok = false;
                    }
                    if (!ok)
                        continue;
                }
                face_mesh.indices.push_back(indices[j * 3 + 0]);
                face_mesh.indices.push_back(indices[j * 3 + 1]);
                face_mesh.indices.push_back(indices[j * 3 + 2]);
                points.push_back(glm::vec4(face_mesh.vertices[indices[j * 3 + 0]], 1.0f));
                points.push_back(glm::vec4(face_mesh.vertices[indices[j * 3 + 1]], 1.0f));
                points.push_back(glm::vec4(face_mesh.vertices[indices[j * 3 + 1]], 1.0f));
                points.push_back(glm::vec4(face_mesh.vertices[indices[j * 3 + 2]], 1.0f));
                points.push_back(glm::vec4(face_mesh.vertices[indices[j * 3 + 2]], 1.0f));
                points.push_back(glm::vec4(face_mesh.vertices[indices[j * 3 + 0]], 1.0f));
            }

            ArPose_destroy(ar_pose);
            ArTrackable_release(face);
        }
        ArTrackableList_destroy(faces);
#endif
    }

    void ARCore::UpdateFeaturePoints() {
        if (!ar_session_ || !ar_frame_) return;
        UpdateAnchor();

        std::vector<glm::vec4> feature_points;
        ArPointCloud *ar_point_cloud = nullptr;
        ArStatus point_cloud_status = ArFrame_acquirePointCloud(ar_session_, ar_frame_, &ar_point_cloud);
        int32_t number_of_points = 0;
        if (point_cloud_status == AR_SUCCESS) {
            //get point cloud
            ArPointCloud_getNumberOfPoints(ar_session_, ar_point_cloud, &number_of_points);
            const float *point_cloud_data;
            ArPointCloud_getData(ar_session_, ar_point_cloud, &point_cloud_data);

            glm::mat4 zero = glm::inverse(GetZeroTransform());
            for (int i = 0; i < number_of_points * 4; i += 4) {
                glm::vec4 raw_pt(point_cloud_data[i + 0], point_cloud_data[i + 1], point_cloud_data[i + 2], 1.0f);
                glm::vec4 trans_pt = zero * raw_pt;
                float conf = point_cloud_data[i + 3];
                feature_points.push_back(glm::vec4(trans_pt.x, trans_pt.y, trans_pt.z, conf));
            }
            ArPointCloud_release(ar_point_cloud);
        }

        std::vector<glm::vec4> depth_points;
        if (useDepth) {
            camera.InitARCore(ar_session_, ar_frame_);

            ArImage* image = nullptr;
            int result = ArFrame_acquireRawDepthImage16Bits(ar_session_, ar_frame_, &image);
            bool isRaw = (result == AR_SUCCESS && image != nullptr);
            if (!isRaw) {
                result = ArFrame_acquireDepthImage16Bits(ar_session_, ar_frame_, &image);
            }
            if (result != AR_SUCCESS || image == nullptr) {
                result = ArFrame_acquireRawDepthImage(ar_session_, ar_frame_, &image);
                isRaw = (result == AR_SUCCESS && image != nullptr);
            }
            if (result != AR_SUCCESS || image == nullptr) {
                result = ArFrame_acquireDepthImage(ar_session_, ar_frame_, &image);
            }
            if (result == AR_SUCCESS && image != nullptr) {
                uint8_t* confidenceData = nullptr;
                ArImage* confidenceImage = nullptr;
                bool hasConfidence = false;
                int32_t confStride = 0;
                if (isRaw) {
                    if (ArFrame_acquireRawDepthConfidenceImage(ar_session_, ar_frame_, &confidenceImage) == AR_SUCCESS && confidenceImage) {
                        int32_t confLen = 0;
                        ArImage_getPlaneRowStride(ar_session_, confidenceImage, 0, &confStride);
                        ArImage_getPlaneData(ar_session_, confidenceImage, 0, (const uint8_t**)&confidenceData, &confLen);
                        if (confidenceData != nullptr && confStride > 0) {
                            hasConfidence = true;
                        } else {
                            ArImage_release(confidenceImage);
                            confidenceImage = nullptr;
                        }
                    }
                }

                uint16_t* imgData = nullptr;
                int64_t timestamp = 0;
                int32_t dataLength = 0;
                int32_t depthWidth = 0, depthHeight = 0, stride = 0;
                ArImage_getPlaneRowStride(ar_session_, image, 0, &stride);
                ArImage_getPlaneData(ar_session_, image, 0, (const uint8_t**)&imgData, &dataLength);
                ArImage_getWidth(ar_session_, image, &depthWidth);
                ArImage_getHeight(ar_session_, image, &depthHeight);
                ArImage_getTimestamp(ar_session_, image, &timestamp);

                if (imgData != nullptr && depthWidth > 0 && depthHeight > 0 && stride > 0) {
                    double len = 100.0 - 0.001f;
                    glm::dmat4 screen2world = glm::inverse(projection_mat * view_mat);
                    float s = 1.0f;
                    if (depthWidth > 240) s = depthWidth / 240.0f;

                    for (float fy = 0; fy < depthHeight; fy += s) {
                        for (float fx = 0; fx < depthWidth; fx += s) {
                            int x = (int)fx;
                            int y = (int)fy;
                            if (x < 0 || x >= depthWidth || y < 0 || y >= depthHeight) continue;

                            if (hasConfidence && confidenceData && confStride > 0) {
                                if (confidenceData[y * confStride + x] == 0) {
                                    continue;
                                }
                            }

                            double depth = (imgData[y * stride / 2 + x] & 0xFFFF) * 0.001f;
                            if (depth > 0.05f && depth < 6.0f) {
                                depth -= offset;
                                glm::vec4 p = ToPoint(screen2world, len, depthWidth, depthHeight, x, y, depth);
                                depth_points.emplace_back(p);
                            }
                        }
                    }
                }

                if (hasConfidence && confidenceImage) {
                    ArImage_release(confidenceImage);
                }
                if (image) {
                    ArImage_release(image);
                }
                lastDepthTimestamp = timestamp;
            }
        }

        if (!depth_points.empty()) {
            points = depth_points;
            for (auto& fp : feature_points) {
                if (fp.w > 0.3f) {
                    points.push_back(fp);
                }
            }
        } else {
            points = feature_points;
        }
        has_coordinate_system_ = true;
    }
}
