/**
 * @file adu_core_interface_ut.cpp
 * @brief Unit tests for adu_core_interface.h
 *
 * @copyright Copyright (c) Microsoft Corp.
 */
#include <catch2/catch.hpp>

#include <algorithm>
#include <sstream>
#include <stdlib.h>

#include "aduc/adu_core_export_helpers.h"
#include "aduc/adu_core_exports.h"
#include "aduc/adu_core_interface.h"
#include "aduc/agent_workflow.h"
#include "aduc/workflow_utils.h"

#define ENABLE_MOCKS
#include "aduc/client_handle_helper.h"
#undef ENABLE_MOCKS

#include "umock_c/umock_c.h"

//
// Test Helpers
//

static void mockIdleCallback(ADUC_Token /*token*/, const char* /*workflowId*/)
{
}

static void mockSandboxDestroyCallback(ADUC_Token /*token*/, const char* /*workflowId*/, const char* /*workFolder**/)
{
}

static ADUC_Result mockSandboxCreateCallback(ADUC_Token /*token*/, const char* /*workflowId*/, char* /*workFolder*/)
{
    return ADUC_Result{ ADUC_Result_Success };
}

static ADUC_Result mockDownloadCallback(
    ADUC_Token /*token*/, const ADUC_WorkCompletionData* /*workCompletionData*/, ADUC_WorkflowDataToken /*info*/)
{
    return ADUC_Result{ ADUC_Result_Success };
}

static std::string escaped(const std::string& input)
{
    std::string output;
    output.reserve(input.size());
    for (const char c : input)
    {
        // more cases can be added here if we can to escape certain characters
        switch (c)
        {
        case '{':
            output += "\\{";
            break;
        case '}':
            output += "\\}";
            break;
        case '+':
            output += "\\+";
            break;
        case '\\':
            output += "\\\\";
            break;
        default:
            output += c;
            break;
        }
    }
    return output;
}

static ADUC_Result mockIsInstalledCallback(ADUC_Token /*token*/, ADUC_WorkflowDataToken /*workflowDataconst*/)
{
    return ADUC_Result{ ADUC_Result_IsInstalled_Installed };
}

class ADUC_UT_ReportPropertyAsyncValues
{
public:
    void
    set(ADUC_ClientHandle deviceHandleIn,
        const unsigned char* reportedStateIn,
        size_t reportedStateLenIn,
        IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallbackIn,
        void* userContextCallbackIn)
    {
        deviceHandle = deviceHandleIn;

        // we interpret the octets to be bytes of a string as it is a json string (utf-8 or ascii is fine).
        // copy the bytes into the std::string wholesale which is bitwise correct.
        reportedState.clear();
        reportedState.reserve(reportedStateLenIn);
        std::for_each(reportedStateIn, reportedStateIn + reportedStateLenIn, [&](unsigned char c) {
            reportedState.push_back(c);
        });

        reportedStateCallback = reportedStateCallbackIn;
        userContextCallback = userContextCallbackIn;
    }

    std::string reportedState;
    ADUC_ClientHandle deviceHandle{ nullptr };
    IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback{ nullptr };
    const void* userContextCallback{ nullptr };
} g_SendReportedStateValues;

static IOTHUB_CLIENT_RESULT mockClientHandle_SendReportedState(
    ADUC_ClientHandle deviceHandle,
    const unsigned char* reportedState,
    size_t reportedStateLen,
    IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback,
    void* userContextCallback)
{
    g_SendReportedStateValues.set(
        deviceHandle, reportedState, reportedStateLen, reportedStateCallback, userContextCallback);

    return IOTHUB_CLIENT_OK;
}

class TestCaseFixture
{
public:
    TestCaseFixture()
    {
        m_previousDeviceHandle = g_iotHubClientHandleForADUComponent;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        g_iotHubClientHandleForADUComponent = reinterpret_cast<ADUC_ClientHandle>(42);

        REGISTER_UMOCK_ALIAS_TYPE(ADUC_ClientHandle, void*);
        REGISTER_UMOCK_ALIAS_TYPE(IOTHUB_CLIENT_REPORTED_STATE_CALLBACK, void*);

        REGISTER_GLOBAL_MOCK_HOOK(ClientHandle_SendReportedState, mockClientHandle_SendReportedState);
    }

    ~TestCaseFixture()
    {
        CHECK(g_iotHubClientHandleForADUComponent != nullptr);
        g_iotHubClientHandleForADUComponent = m_previousDeviceHandle;
    }

    TestCaseFixture(const TestCaseFixture&) = delete;
    TestCaseFixture& operator=(const TestCaseFixture&) = delete;
    TestCaseFixture(TestCaseFixture&&) = delete;
    TestCaseFixture& operator=(TestCaseFixture&&) = delete;

private:
    ADUC_ClientHandle m_previousDeviceHandle;
};

//
// Test Cases
//

TEST_CASE_METHOD(TestCaseFixture, "AzureDeviceUpdateCoreInterface_Create")
{
    void* context = nullptr;

    CHECK(AzureDeviceUpdateCoreInterface_Create(&context, 0 /*argc*/, nullptr /*argv*/));
    CHECK(context != nullptr);

    AzureDeviceUpdateCoreInterface_Destroy(&context);
}

const char* action_bundle_download =
    R"( { )"
    R"(     "updateManifest": "{\"manifestVersion\":\"2.0\",\"updateId\":{\"provider\":\"Contoso\",\"name\":\"VacuumBundleUpdate\",\"version\":\"1.0\"},\"updateType\":\"microsoft/bundle:1\",\"installedCriteria\":\"1.0\",\"files\":{\"00000\":{\"fileName\":\"contoso-motor-1.0-updatemanifest.json\",\"sizeInBytes\":1396,\"hashes\":{\"sha256\":\"E2o94XQss/K8niR1pW6OdaIS/y3tInwhEKMn/6Rw1Gw=\"}}},\"createdDateTime\":\"2021-06-07T07:25:59.0781905Z\"}",     )"
    R"(     "updateManifestSignature": "eyJhbGciOiJSUzI1NiIsInNqd2siOiJleUpoYkdjaU9pSlNVekkxTmlJc0ltdHBaQ0k2SWtGRVZTNHlNREEzTURJdVVpSjkuZXlKcmRIa2lPaUpTVTBFaUxDSnVJam9pY2toV1FrVkdTMUl4ZG5Ob1p5dEJhRWxuTDFORVVVOHplRFJyYWpORFZWUTNaa2R1U21oQmJYVkVhSFpJWm1velowaDZhVEJVTWtsQmNVTXhlREpDUTFka1QyODFkamgwZFcxeFVtb3ZibGx3WnprM2FtcFFRMHQxWTJSUE5tMHpOMlJqVDIxaE5EWm9OMDh3YTBod2Qwd3pibFZJUjBWeVNqVkVRUzloY0ZsdWQwVmxjMlY0VkdwVU9GTndMeXRpVkhGWFJXMTZaMFF6TjNCbVpFdGhjV3AwU0V4SFZtbFpkMVpJVUhwMFFtRmlkM2RxYUVGMmVubFNXUzk1T1U5bWJYcEVabGh0Y2xreGNtOHZLekpvUlhGRmVXdDFhbmRSUlZscmFHcEtZU3RDTkRjMkt6QnRkVWQ1VjBrMVpVbDJMMjlzZERKU1pWaDRUV0k1VFd4c1dFNTViMUF6WVU1TFNVcHBZbHBOY3pkMVMyTnBkMnQ1YVZWSllWbGpUV3B6T1drdlVrVjVLMnhOT1haSlduRnlabkJEVlZoMU0zUnVNVXRuWXpKUmN5OVVaRGgwVGxSRFIxWTJkM1JXWVhGcFNYQlVaRlEwVW5KRFpFMXZUelZUVG1WbVprUjVZekpzUXpkMU9EVXJiMjFVYTJOcVVHcHRObVpoY0dSSmVVWXljV1Z0ZGxOQ1JHWkNOMk5oYWpWRVNVa3lOVmQzTlVWS1kyRjJabmxRTlRSdGNVNVJVVE5IWTAxUllqSmtaMmhwWTJ4d2FsbHZLelF6V21kWlEyUkhkR0ZhWkRKRlpreGFkMGd6VVdjeWNrUnNabXN2YVdFd0x6RjVjV2xyTDFoYU1XNXpXbFJwTUVKak5VTndUMDFGY1daT1NrWlJhek5DVjI5Qk1EVnlRMW9pTENKbElqb2lRVkZCUWlJc0ltRnNaeUk2SWxKVE1qVTJJaXdpYTJsa0lqb2lRVVJWTGpJd01EY3dNaTVTTGxNaWZRLmlTVGdBRUJYc2Q3QUFOa1FNa2FHLUZBVjZRT0dVRXV4dUhnMllmU3VXaHRZWHFicE0takk1UlZMS2VzU0xDZWhLLWxSQzl4Ni1fTGV5eE5oMURPRmMtRmE2b0NFR3dVajh6aU9GX0FUNnM2RU9tY2txUHJ4dXZDV3R5WWtrRFJGNzRkdGFLMWpOQTdTZFhyWnp2V0NzTXFPVU1OejBnQ29WUjBDczEyNTRrRk1SbVJQVmZFY2pnVDdqNGxDcHlEdVdncjlTZW5TZXFnS0xZeGphYUcwc1JoOWNkaTJkS3J3Z2FOYXFBYkhtQ3JyaHhTUENUQnpXTUV4WnJMWXp1ZEVvZnlZSGlWVlJoU0pwajBPUTE4ZWN1NERQWFYxVGN0MXkzazdMTGlvN244aXpLdXEybTNUeEY5dlBkcWI5TlA2U2M5LW15YXB0cGJGcEhlRmtVTC1GNXl0bF9VQkZLcHdOOUNMNHdwNnlaLWpkWE5hZ3JtVV9xTDFDeVh3MW9tTkNnVG1KRjNHZDNseXFLSEhEZXJEcy1NUnBtS2p3U3dwWkNRSkdEUmNSb3ZXeUwxMnZqdzNMQkpNaG1VeHNFZEJhWlA1d0dkc2ZEOGxkS1lGVkZFY1owb3JNTnJVa1NNQWw2cEl4dGVmRVhpeTVscW1pUHpxX0xKMWVSSXJxWTBfIn0.eyJzaGEyNTYiOiI3alo1YWpFN2Z5SWpzcTlBbWlKNmlaQlNxYUw1bkUxNXZkL0puVWgwNFhZPSJ9.EK5zcNiEgO2rHh_ichQWlDIvkIsPXrPMQK-0D5WK8ZnOR5oJdwhwhdpgBaB-tE-6QxQB1PKurbC2BtiGL8HI1DgQtL8Fq_2ASRfzgNtrtpp6rBiLRynJuWCy7drgM6g8WoSh8Utdxsx5lnGgAVAU67ijK0ITd0E70R7vWJRmY8YxxDh-Sh8BNz68pvU-YJQwKtVy64lD5zA0--BL432F-uZWTc6n-BduQdSB4J7Eu6zGlT75s8Ehd-SIylsstu4wdypU0tcwIH-MaSKcH5mgEmokaHncJrb4zKnZwxYQUeDMoFjF39P9hDmheHywY1gwYziXjUcnMn8_T00oMeycQ7PDCTJHIYB3PGbtM9KiA3RQH-08ofqiCVgOLeqbUHTP03Z0Cx3e02LzTgP8_Lerr4okAUPksT2IGvvsiMtj04asdrLSlv-AvFud-9U0a2mJEWcosI04Q5NAbqhZ5ZBzCkkowLGofS04SnfS-VssBfmbH5ue5SWb-AxBv1inZWUj", )"
    R"(     "workflow": {   )"
    R"(         "id": "action_bundle", )"
    R"(         "action": 0 )"
    R"(     }, )"
    R"(     "fileUrls": {   )"
    R"(         "00000": "file:///tmp/tests/testfiles/contoso-motor-1.0-updatemanifest.json",  )"
    R"(         "00001": "file:///tmp/tests/testfiles/contoso-motor-1.0-installer",     )"
    R"(         "gw001": "file:///tmp/tests/testfiles/behind-gateway-info.json" )"
    R"(     } )"
    R"( } )";

const char* action_bundle_cancel = R"( { )"
                                   R"(     "updateManifest": "",     )"
                                   R"(     "updateManifestSignature": "", )"
                                   R"(     "workflow": {   )"
                                   R"(         "id": "action_bundle", )"
                                   R"(         "action": 255 )"
                                   R"(     } )"
                                   R"( } )";

TEST_CASE_METHOD(TestCaseFixture, "AzureDeviceUpdateCoreInterface_Connected")
{
    // Init workflow.
    ADUC_WorkflowData workflowData{};
    ADUC_WorkflowHandle bundle = nullptr;
    ADUC_Result result = workflow_init(action_bundle_download, false, &bundle);
    CHECK(bundle != nullptr);

    workflowData.WorkflowHandle = bundle;
    CHECK(result.ResultCode != 0);

    // Typically Register would initialize the IdleCallback.
    workflowData.UpdateActionCallbacks.IdleCallback = mockIdleCallback;
    // Typically Register would initialize the DownloadCallback.
    workflowData.UpdateActionCallbacks.DownloadCallback = mockDownloadCallback;
    // Typically Register would initialize the IsInstalledCallback.
    workflowData.UpdateActionCallbacks.IsInstalledCallback = mockIsInstalledCallback;
    // Typically Register would initialize the SandboxDestroyCallback.
    workflowData.UpdateActionCallbacks.SandboxDestroyCallback = mockSandboxDestroyCallback;
    // Typically Register would initialize the SandboxCreateCallback.
    workflowData.UpdateActionCallbacks.SandboxCreateCallback = mockSandboxCreateCallback;

    AzureDeviceUpdateCoreInterface_Connected(&workflowData);

    int lastReportedState = workflow_get_last_reported_state();

    // Recieving 'download' action, and mockDwonloadCallback return success.
    // The expected reported state is ADUCITF_State_DownloadSucceeded.
    CHECK(lastReportedState == ADUCITF_State_DownloadSucceeded);
    CHECK(workflowData.StartupIdleCallSent);
    CHECK(!workflow_get_operation_in_progress(workflowData.WorkflowHandle));
    CHECK(!workflow_get_operation_cancel_requested(workflowData.WorkflowHandle));
}

TEST_CASE_METHOD(TestCaseFixture, "AzureDeviceUpdateCoreInterface_ReportStateAndResultAsync")
{
    SECTION("Download Success")
    {
        ADUC_Result result{ ADUC_Result_Download_Success, 0 };
        // Init workflow.
        ADUC_WorkflowData workflowData{};
        ADUC_WorkflowHandle bundle = nullptr;
        result = workflow_init(action_bundle_cancel, false, &bundle);
        workflowData.WorkflowHandle = bundle;
        CHECK(result.ResultCode != 0);

        const ADUCITF_State updateState = ADUCITF_State_DownloadSucceeded;
        result.ResultCode = ADUC_Result_Download_Success;
        AzureDeviceUpdateCoreInterface_ReportStateAndResultAsync(
            &workflowData, updateState, &result, nullptr /* installedUpdateId */);

        CHECK(g_SendReportedStateValues.deviceHandle != nullptr);
        std::stringstream strm;

#ifndef ENABLE_ADU_TELEMETRY_REPORTING
        // clang-format off
        strm << R"({)"
                << R"("deviceUpdate":{)"
                    << R"("__t":"c",)"
                    << R"("agent":{)"
                        << R"("lastInstallResult":{)"
                            << R"("updateInstallResult":{)"
                                << R"("resultCode":)" << static_cast<unsigned int>(ADUC_Result_Download_Success) << R"(,)"
                                << R"("extendedResultCode":0,)"
                                << R"("resultDetails":"")"
                            << R"(})"
                        << R"(},)"
                        << R"("state":)" << static_cast<unsigned int>(updateState) << R"(},)"
                        << R"("workflow":{)"
                            << R"("action":3,)"
                            << R"("id":"action_bundle")"
                        << R"(})"
                    << R"(})"
                << R"(})"
             << R"(})";
        // clang-format on
        CHECK(g_SendReportedStateValues.reportedState == strm.str());
#else
        // clang-format off
        strm << R"({)"
                << R"("deviceUpdate":{)"
                    << R"("__t":"c",)"
                    << R"("agent":{)"
                        << R"("lastInstallResult":{)"
                            << R"("updateInstallResult":{)"
                                << R"("resultCode":)" << static_cast<unsigned int>(ADUC_Result_Download_Success) << R"(,)"
                                << R"("extendedResultCode":0,)"
                                << R"("resultDetails":"")"
                            << R"(})"
                        << R"(},)"
                        << R"("state":)" << static_cast<unsigned int>(updateState) << R"(,)"
                        << R"("workflow":{)"
                            << R"("action":3,)"
                            << R"("id":"action_bundle")"
                        << R"(})"
                    << R"(})"
                << R"(})"
             << R"(})";
        // clang-format on
        CHECK(g_SendReportedStateValues.reportedState == strm.str());
#endif
        CHECK(g_SendReportedStateValues.reportedStateCallback != nullptr);
        CHECK(g_SendReportedStateValues.userContextCallback == nullptr);
    }

    SECTION("Failed")
    {
        ADUC_Result result;

        const ADUCITF_State updateState = ADUCITF_State_Failed;

        // Init workflow.
        ADUC_WorkflowData workflowData{};
        ADUC_WorkflowHandle bundle = nullptr;
        result = workflow_init(action_bundle_cancel, false, &bundle);
        workflowData.WorkflowHandle = bundle;
        CHECK(result.ResultCode != 0);

        result = { ADUC_Result_Failure, ADUC_ERC_NOTPERMITTED };
        AzureDeviceUpdateCoreInterface_ReportStateAndResultAsync(
            &workflowData, updateState, &result, nullptr /* installedUpdateId */);

        CHECK(g_SendReportedStateValues.deviceHandle != nullptr);
        std::stringstream strm;

#ifndef ENABLE_ADU_TELEMETRY_REPORTING
        // clang-format off
        strm << R"({)"
                << R"("deviceUpdate":{)"
                    << R"("__t":"c",)"
                    << R"("agent":{)"
                        << R"("lastInstallResult":{)"
                            << R"("updateInstallResult":{)"
                                << R"("resultCode":)" << static_cast<unsigned int>(ADUC_Result_Failure) << R"(,)"
                                << R"("extendedResultCode":)"<< ADUC_ERC_NOTPERMITTED << R"(,)"
                                << R"("resultDetails":"")"
                            << R"(})"
                        << R"(},)"
                        << R"("state":)" << static_cast<unsigned int>(updateState) << R"(,)"
                        << R"("workflow":{)"
                            << R"("action":3,)"
                            << R"("id":"action_bundle")"
                        << R"(})"
                    << R"(})"
                << R"(})"
                << R"(})";
        // clang-format on
        CHECK(g_SendReportedStateValues.reportedState == strm.str());
#else
        // clang-format off
        strm << R"({)"
                << R"("deviceUpdate":{)"
                    << R"("__t":"c",)"
                    << R"("agent":{)"
                        << R"("lastInstallResult":{)"
                            << R"("updateInstallResult":{)"
                                << R"("resultCode":)" << static_cast<unsigned int>(ADUC_Result_Failure) << R"(,)"
                                << R"("extendedResultCode":)"<< ADUC_ERC_NOTPERMITTED << R"(,)"
                                << R"("resultDetails":"")"
                            << R"(})"
                        << R"(},)"
                        << R"("state":)" << static_cast<unsigned int>(updateState) << R"(,)"
                        << R"("workflow":{)"
                            << R"("action":3,)"
                            << R"("id":"action_bundle")"
                        << R"(})"
                    << R"(})"
                << R"(})"
                << R"(})";
        // clang-format on
        CHECK(g_SendReportedStateValues.reportedState == strm.str());
#endif
        CHECK(g_SendReportedStateValues.reportedStateCallback != nullptr);
        CHECK(g_SendReportedStateValues.userContextCallback == nullptr);
    }
}

TEST_CASE_METHOD(TestCaseFixture, "AzureDeviceUpdateCoreInterface_ReportContentIdAndIdleAsync")
{
    ADUC_Result result;

    const std::string provider{ "Microsoft" };
    const std::string name{ "adu" };
    const std::string version{ "1.2.3.4" };

    std::stringstream installedUpdateIdStr;

    // clang-format off
    installedUpdateIdStr
                    << R"({)"
                        << R"(\"provider\":\")" << provider << R"(\",)"
                        << R"(\"name\":\")"<< name << R"(\",)"
                        << R"(\"version\":\")" << version << R"(\")"
                    << R"(})";
    // clang-format on

    ADUC_UpdateId* updateId = ADUC_UpdateId_AllocAndInit(provider.c_str(), name.c_str(), version.c_str());
    CHECK(updateId != nullptr);

    // Init workflow since workflow needs a valid workflow handle to get the workflow id.
    ADUC_WorkflowData workflowData{};
    ADUC_WorkflowHandle bundle = nullptr;
    result = workflow_init(action_bundle_download, false, &bundle);
    workflowData.WorkflowHandle = bundle;
    CHECK(result.ResultCode != 0);

    AzureDeviceUpdateCoreInterface_ReportUpdateIdAndIdleAsync(&workflowData, installedUpdateIdStr.str().c_str());

    CHECK(g_SendReportedStateValues.deviceHandle != nullptr);

    std::stringstream strm;
    // clang-format off
     strm << R"({)"
                << R"("deviceUpdate":{)"
                    << R"("__t":"c",)"
                    << R"("agent":{)"
                        << R"("lastInstallResult":{)"
                            << R"("updateInstallResult":{)"
                                << R"("resultCode":700,)"
                                << R"("extendedResultCode":0,)"
                                << R"("resultDetails":"")"
                            << R"(})"
                        << R"(},)"
                        << R"("state":)" << static_cast<unsigned int>(ADUCITF_State_Idle) << R"(,)"
                        << R"("workflow":{)"
                            << R"("action":3,)"
                            << R"("id":"action_bundle")"
                        << R"(})"
                        << R"(,)"
                        << R"("installedUpdateId":"{\\\"provider\\\":\\\"Microsoft\\\",\\\"name\\\":\\\"adu\\\",\\\"version\\\":\\\"1.2.3.4\\\"}")"
                    << R"(})"
                << R"(})"
            << R"(})";
    // clang-format on
    CHECK(g_SendReportedStateValues.reportedState == strm.str());
    CHECK(g_SendReportedStateValues.reportedStateCallback != nullptr);
    CHECK(g_SendReportedStateValues.userContextCallback == nullptr);

    ADUC_UpdateId_UninitAndFree(updateId);
}
