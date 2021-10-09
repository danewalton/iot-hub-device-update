#
# Device Update for IoT Hub
# PowerShell module for creating and preparing update for import into Device Update for IoT Hub (ADU).
# Copyright (c) Microsoft Corporation.
#

#Requires -Version 5.0
using module .\AduUpdate.psm1
Import-Module $PSScriptRoot\AduAzStorageBlobHelper.psm1 -ErrorAction Stop

function New-AduImportUpdateInput
{
<#
    .SYNOPSIS
        Create a new ADU update import input and stage relevant artifacts in the provided Azure Storage Blob container.

    .EXAMPLE
        PS > $updateId = New-AduUpdateId -Provider Fabrikam -Name Toaster -Version 2.0
        PS > $compatInfo1 = New-AduUpdateCompatibility -DeviceManufacturer Fabrikam -DeviceModel Toaster
        PS > $compatInfo2 = New-AduUpdateCompatibility -DeviceManufacturer Contoso -DeviceModel Toaster
        PS >
        PS > New-AduImportUpdateInput -UpdateId $updateId `
                                      -UpdateType microsoft/swupdate:1 -InstalledCriteria 5 `
                                      -Compatibility $compatInfo1, $compatInfo2 `
                                      -Files '.\file1.json', '.\file2.zip'
#>
    [CmdletBinding()]
    Param(
        # Update identity created using New-AduUpdateId.
        [Parameter(Mandatory=$true)]
        [ValidateNotNull()]
        [UpdateId] $UpdateId,

        # Update type in form of "{provider}/{updateType}:{updateTypeVersion}"
        # This parameter is forwarded to client device during deployment. It may be used to determine whether this update is of supported types.
        # For example, reference ADU agent uses the following:
        # - "microsoft/swupdate:1" for SwUpdate image-based update.
        # - "microsoft/apt:1" for APT package-based update.
        [Parameter(Mandatory=$true)]
        [ValidateLength(1, 32)]
        [ValidatePattern("^\S+\/\S+:\d{1,5}$")]
        [string] $UpdateType,

        # Criteria for update to be considered installed.
        # This parameter is forwarded to client device during deployment.
        # For example, reference ADU agent expects the following value:
        # - Value of SwVersion for SwUpdate image-based update.
        # - '{name}-{version}' of which name and version are obtained from the APT file.
        [Parameter(Mandatory=$true)]
        [ValidateLength(1, 64)]
        [string] $InstalledCriteria,

        # List of compatibility information of devices this update is compatible with, created using New-AduUpdateCompatibility.
        [Parameter(Mandatory=$true)]
        [ValidateCount(1, 10)]
        [hashtable[]] $Compatibility,

        # List of full paths to update files.
        [ValidateCount(0, 5)]
        [string[]] $Files = @(),

        # List of bundled updateIds.
        [ValidateCount(0, 10)]
        [UpdateId[]] $BundledUpdates = @(),

        # Whether the update can be deployed on its own to a device. Must be false for a leaf (bundled) update.
        [bool] $IsDeployable = $true,

        # Azure Storage Blob container to host the files.
        [Parameter(Mandatory=$true)]
        [ValidateNotNullOrEmpty()]
        [Microsoft.WindowsAzure.Commands.Common.Storage.ResourceModel.AzureStorageContainer] $BlobContainer
    )

    $importMan = New-AduImportManifest -UpdateId $UpdateId -Files $Files -BundledUpdates $BundledUpdates -IsDeployable $IsDeployable `
                                       -Compatibility $Compatibility -UpdateType $UpdateType -InstalledCriteria $InstalledCriteria `
                                       -ErrorAction Stop

    $importManJsonFile = New-TemporaryFile
    $importMan | Out-File $importManJsonFile -Encoding utf8

    Get-Content $importManJsonFile | Write-Verbose

    Write-Verbose "Uploading import manifest to Azure Blob Storage."
    $importManJsonFile = Get-Item $importManJsonFile # refresh file properties
    $importManJsonHash = Get-AduFileHashes -FilePath $importManJsonFile -ErrorAction Stop
    $updateIdStr = "$($UpdateId.Provider).$($UpdateId.Name).$($UpdateId.Version)"
    $importManUrl = Get-AduAzBlobSASToken -FilePath $importManJsonFile -BlobName "$updateIdStr/importmanifest.json" -BlobContainer $BlobContainer -ErrorAction Stop

    Write-Verbose "Uploading update file(s) to Azure Blob Storage."
    $fileMetaList = @()

    $Files | ForEach-Object {
        # Place files within updateId subdirectory in case there are filenames conflict.
        $filename = Split-Path -Leaf $_
        $blobName = "$updateIdStr/$filename"

        $url = Get-AduAzBlobSASToken -FilePath $_ -BlobName $blobName -BlobContainer $BlobContainer -ErrorAction Stop

        $fileMeta = New-Object PSObject | Select-Object FileName, Url
        $fileMeta.FileName = (Split-Path $_ -Leaf)
        $fileMeta.Url = $url

        $fileMetaList += $fileMeta
    }

    Write-Verbose "Preparing Import Update API request body."
    $importManMeta = [PSCustomObject]@{
        Url = $importManUrl
        SizeInBytes = $importManJsonFile.Length
        Hashes = $importManJsonHash
    }

    $importUpdateInput = [PSCustomObject]@{
        ImportManifest = $importManMeta
        Files = $fileMetaList
    }

    Remove-Item $importManJsonFile

    return $importUpdateInput
}

Export-ModuleMember -Function New-AduImportUpdateInput, New-AduUpdateId, New-AduUpdateCompatibility, Get-AduAzBlobContainer