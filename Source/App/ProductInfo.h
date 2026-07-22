#pragma once

#include "ProductInfoGenerated.h"

#include <string_view>

namespace padflow::product {
inline constexpr std::string_view name{PADFLOW_PRODUCT_NAME_VALUE};
inline constexpr std::string_view company{PADFLOW_COMPANY_NAME_VALUE};
inline constexpr std::string_view bundleId{PADFLOW_BUNDLE_ID_VALUE};
inline constexpr std::string_view version{PADFLOW_VERSION_VALUE};
inline constexpr std::string_view projectExtension{PADFLOW_PROJECT_EXTENSION_VALUE};
inline constexpr int schemaVersion = PADFLOW_SCHEMA_VERSION_VALUE;
} // namespace padflow::product
