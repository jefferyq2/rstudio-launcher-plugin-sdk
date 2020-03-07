/*
 * Response.cpp
 *
 * Copyright (C) 2019 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant to the terms of a commercial license agreement
 * with RStudio, then this program is licensed to you under the following terms:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <api/Response.hpp>

#include <atomic>

#include <json/Json.hpp>
#include "Constants.hpp"

namespace rstudio {
namespace launcher_plugins {
namespace api {

// Response ============================================================================================================
struct Response::Impl
{
   /**
    * @brief Constructor.
    *
    * @param in_responseType    The type of the response.
    * @param in_requestId       The ID of the request for which this response is being sent.
    */
   Impl(Type in_responseType, uint64_t in_requestId) :
      ResponseType(static_cast<int>(in_responseType)),
      RequestId(in_requestId),
      ResponseId(
         ((in_responseType == Type::HEARTBEAT) || (in_responseType == Type::ERROR)) ?
            0 : NEXT_RESPONSE_ID.fetch_add(1))
   {
   }

   /** Global atomic value to keep track of all response IDs used so far. */
   static std::atomic_uint64_t NEXT_RESPONSE_ID;

   /** The type of the response. */
   const int ResponseType;

   /** The ID of the request for which this response is being sent. */
   const uint64_t RequestId;

   /** The ID of this response. */
   const uint64_t ResponseId;
};

std::atomic_uint64_t Response::Impl::NEXT_RESPONSE_ID { 0 };

PRIVATE_IMPL_DELETER_IMPL(Response)

json::Object Response::toJson() const
{
   json::Object jsonObject;
   jsonObject.insert(FIELD_MESSAGE_TYPE, json::Value(m_responseImpl->ResponseType));
   jsonObject.insert(FIELD_REQUEST_ID, json::Value(m_responseImpl->RequestId));
   jsonObject.insert(FIELD_RESPONSE_ID, json::Value(m_responseImpl->ResponseId));

   return jsonObject;
}

Response::Response(Type in_responseType, uint64_t in_requestId) :
   m_responseImpl(new Impl(in_responseType, in_requestId))
{
}

// Bootstrap Response ==================================================================================================
BootstrapResponse::BootstrapResponse(uint64_t in_requestId) :
   Response(Type::BOOTSTRAP, in_requestId)
{
}

json::Object BootstrapResponse::toJson() const
{
   // Get the object generated by the base class.
   json::Object jsonObject = Response::toJson();

   // Add the bootstrap specific fields.
   json::Object version;
   version.insert(FIELD_VERSION_MAJOR, json::Value(API_VERSION_MAJOR));
   version.insert(FIELD_VERSION_MINOR, json::Value(API_VERSION_MINOR));
   version.insert(FIELD_VERSION_PATCH, json::Value(API_VERSION_PATCH));

   jsonObject.insert(FIELD_VERSION, version);
   return jsonObject;
}

// Error Response ======================================================================================================
struct ErrorResponse::Impl
{
   /**
    * @brief Constructor.
    *
    * @param in_errorType       The type of error.
    * @param in_errorMessage    The message of the error.
    */
   Impl(Type in_errorType, std::string in_errorMessage) :
      ErrorCode(static_cast<int>(in_errorType)),
      ErrorMessage(std::move(in_errorMessage))
   {
   }

   /** The error code. */
   int ErrorCode;

   /** The error message. */
   std::string ErrorMessage;
};

PRIVATE_IMPL_DELETER_IMPL(ErrorResponse)

ErrorResponse::ErrorResponse(uint64_t in_requestId, Type in_errorType, std::string in_errorMessage) :
   Response(Response::Type::ERROR, in_requestId),
   m_impl(new Impl(in_errorType, std::move(in_errorMessage)))
{
}

json::Object ErrorResponse::toJson() const
{
   json::Object responseObject = Response::toJson();
   responseObject.insert(FIELD_ERROR_CODE, json::Value(m_impl->ErrorCode));
   responseObject.insert(FIELD_ERROR_MESSAGE, json::Value(m_impl->ErrorMessage));

   return responseObject;
}

// Heartbeat Response ==================================================================================================
HeartbeatResponse::HeartbeatResponse() :
   Response(Response::Type::HEARTBEAT, 0)
{
}

// Cluster Info Response ===============================================================================================
struct ClusterInfoResponse::Impl
{
   Impl(
      std::vector<std::string> in_queues,
      std::vector<ResourceLimit> in_resourceLimits,
      std::vector<PlacementConstraint> in_placementConstraints,
      std::vector<JobConfig> in_config) :
         AllowUnknownImages(false),
         Config(std::move(in_config)),
         PlacementConstraints(std::move(in_placementConstraints)),
         Queues(std::move(in_queues)),
         ResourceLimits(std::move(in_resourceLimits)),
         SupportsContainers(false)
   {
   }

   Impl(
      std::set<std::string> in_containerImages,
      std::string in_defaultImage,
      bool in_allowUnknownImages,
      std::vector<std::string> in_queues,
      std::vector<ResourceLimit> in_resourceLimits,
      std::vector<PlacementConstraint> in_placementConstraints ,
      std::vector<JobConfig> in_config) :
         AllowUnknownImages(in_allowUnknownImages),
         Config(std::move(in_config)),
         ContainerImages(std::move(in_containerImages)),
         DefaultImage(std::move(in_defaultImage)),
         PlacementConstraints(std::move(in_placementConstraints)),
         Queues(std::move(in_queues)),
         ResourceLimits(std::move(in_resourceLimits)),
         SupportsContainers(true)
   {
   }

   bool AllowUnknownImages;
   std::vector<JobConfig> Config;
   std::set<std::string> ContainerImages;
   std::string DefaultImage;
   std::vector<PlacementConstraint> PlacementConstraints;
   std::vector<std::string> Queues;
   std::vector<ResourceLimit> ResourceLimits;
   bool SupportsContainers;
};

PRIVATE_IMPL_DELETER_IMPL(ClusterInfoResponse)

ClusterInfoResponse::ClusterInfoResponse(
   uint64_t in_requestId,
   std::vector<std::string> in_queues,
   std::vector<ResourceLimit> in_resourceLimits,
   std::vector<PlacementConstraint> in_placementConstraints,
   std::vector<JobConfig> in_config) :
      Response(Response::Type::CLUSTER_INFO, in_requestId),
      m_impl(
         new Impl(
            std::move(in_queues),
            std::move(in_resourceLimits),
            std::move(in_placementConstraints),
            std::move(in_config)))
{
}

ClusterInfoResponse::ClusterInfoResponse(
   uint64_t in_requestId,
   std::set<std::string> in_containerImages,
   std::string in_defaultImage,
   bool in_allowUnknownImages,
   std::vector<std::string> in_queues,
   std::vector<ResourceLimit> in_resourceLimits,
   std::vector<PlacementConstraint> in_placementConstraints ,
   std::vector<JobConfig> in_config) :
      Response(Response::Type::CLUSTER_INFO, in_requestId),
      m_impl(new Impl(
         std::move(in_containerImages),
         std::move(in_defaultImage),
         in_allowUnknownImages,
         std::move(in_queues),
         std::move(in_resourceLimits),
         std::move(in_placementConstraints),
         std::move(in_config)))
{
}

json::Object ClusterInfoResponse::toJson() const
{
   json::Object result = Response::toJson();

   result[FIELD_CONTAINER_SUPPORT] = m_impl->SupportsContainers;

   if (m_impl->SupportsContainers)
   {
      if (!m_impl->DefaultImage.empty())
         result[FIELD_DEFAULT_IMAGE] = m_impl->DefaultImage;

      result[FIELD_ALLOW_UNKNOWN_IMAGES] = m_impl->AllowUnknownImages;
      result[FIELD_IMAGES] = json::toJsonArray(m_impl->ContainerImages);
   }

   if (!m_impl->Queues.empty())
      result[FIELD_QUEUES] = json::toJsonArray(m_impl->Queues);

   json::Array config, constraints, limits;

   for (const JobConfig& configVal: m_impl->Config)
      config.push_back(configVal.toJson());

   for (const PlacementConstraint& constraint: m_impl->PlacementConstraints)
      constraints.push_back(constraint.toJson());

   for (const ResourceLimit& limit: m_impl->ResourceLimits)
      limits.push_back(limit.toJson());

   result[FIELD_CONFIG] = config;
   result[FIELD_RESOURCE_LIMITS] = limits;
   result[FIELD_PLACEMENT_CONSTRAINTS] = constraints;

   return result;
}

} // namespace api
} // namespace launcher_plugins
} // namespace rstudio
