/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.permission

import android.Manifest
import mozilla.components.concept.engine.permission.Permission
import mozilla.components.support.test.mock
import mozilla.components.test.ReflectionUtils
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.Mockito.verify
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission.VALUE_ALLOW
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission.VALUE_DENY
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.MediaSource
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.PERMISSION_GEOLOCATION

class GeckoPermissionRequestTest {

    @Test
    fun `create content permission request`() {
        val uri = "https://mozilla.org"

        var request = GeckoPermissionRequest.Content(uri, PERMISSION_DESKTOP_NOTIFICATION, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.ContentNotification()), request.permissions)

        request = GeckoPermissionRequest.Content(uri, PERMISSION_GEOLOCATION, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.ContentGeoLocation()), request.permissions)

        request = GeckoPermissionRequest.Content(uri, GeckoSession.PermissionDelegate.PERMISSION_AUTOPLAY_AUDIBLE, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.ContentAutoPlayAudible()), request.permissions)

        request = GeckoPermissionRequest.Content(uri, GeckoSession.PermissionDelegate.PERMISSION_AUTOPLAY_INAUDIBLE, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.ContentAutoPlayInaudible()), request.permissions)

        request = GeckoPermissionRequest.Content(uri, GeckoSession.PermissionDelegate.PERMISSION_LOCAL_DEVICE_ACCESS, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.ContentLocalDeviceAccess()), request.permissions)

        request = GeckoPermissionRequest.Content(uri, GeckoSession.PermissionDelegate.PERMISSION_LOCAL_NETWORK_ACCESS, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.ContentLocalNetworkAccess()), request.permissions)

        request = GeckoPermissionRequest.Content(uri, 1234, mock(), mock())
        assertEquals(uri, request.uri)
        assertEquals(listOf(Permission.Generic("1234", "Gecko permission type = 1234")), request.permissions)
    }

    @Test
    fun `grant content permission request`() {
        val uri = "https://mozilla.org"
        val geckoResult = mock<GeckoResult<Int>>()

        val request = GeckoPermissionRequest.Content(uri, PERMISSION_GEOLOCATION, mock(), mutableListOf(geckoResult))

        assertFalse(request.isCompleted)

        request.grant()

        verify(geckoResult).complete(VALUE_ALLOW)
        assertTrue(request.isCompleted)
    }

    @Test
    fun `reject content permission request`() {
        val uri = "https://mozilla.org"
        val geckoResult = mock<GeckoResult<Int>>()

        val request = GeckoPermissionRequest.Content(uri, PERMISSION_GEOLOCATION, mock(), mutableListOf(geckoResult))

        assertFalse(request.isCompleted)

        request.reject()
        verify(geckoResult).complete(VALUE_DENY)
        assertTrue(request.isCompleted)
    }

    @Test
    fun `create app permission request`() {
        val callback: GeckoSession.PermissionDelegate.Callback = mock()
        val permissions = listOf(
            Manifest.permission.ACCESS_COARSE_LOCATION,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO,
            "unknown app permission",
        )

        val mappedPermissions = listOf(
            Permission.AppLocationCoarse(Manifest.permission.ACCESS_COARSE_LOCATION),
            Permission.AppLocationFine(Manifest.permission.ACCESS_FINE_LOCATION),
            Permission.AppCamera(Manifest.permission.CAMERA),
            Permission.AppAudio(Manifest.permission.RECORD_AUDIO),
            Permission.Generic("unknown app permission"),
        )

        val request = GeckoPermissionRequest.App(permissions, mutableListOf(callback))
        assertEquals(mappedPermissions, request.permissions)
    }

    @Test
    fun `grant app permission request`() {
        val callback: GeckoSession.PermissionDelegate.Callback = mock()

        val request = GeckoPermissionRequest.App(listOf(Manifest.permission.CAMERA), mutableListOf(callback))
        request.grant()
        verify(callback).grant()
    }

    @Test
    fun `reject app permission request`() {
        val callback: GeckoSession.PermissionDelegate.Callback = mock()

        val request = GeckoPermissionRequest.App(listOf(Manifest.permission.CAMERA), mutableListOf(callback))
        request.reject()
        verify(callback).reject()
    }

    @Test
    fun `create media permission request`() {
        val callback: GeckoSession.PermissionDelegate.MediaCallback = mock()
        val uri = "https://mozilla.org"

        val audioMicrophone = MockMediaSource(
            "audioMicrophone",
            "audioMicrophone",
            MediaSource.SOURCE_MICROPHONE,
            MediaSource.TYPE_AUDIO,
        )
        val audioCapture = MockMediaSource(
            "audioCapture",
            "audioCapture",
            MediaSource.SOURCE_AUDIOCAPTURE,
            MediaSource.TYPE_AUDIO,
        )
        val audioOther = MockMediaSource(
            "audioOther",
            "audioOther",
            MediaSource.SOURCE_OTHER,
            MediaSource.TYPE_AUDIO,
        )

        val videoCamera = MockMediaSource(
            "videoCamera",
            "videoCamera",
            MediaSource.SOURCE_CAMERA,
            MediaSource.TYPE_VIDEO,
        )
        val videoScreen = MockMediaSource(
            "videoScreen",
            "videoScreen",
            MediaSource.SOURCE_SCREEN,
            MediaSource.TYPE_VIDEO,
        )
        val videoOther = MockMediaSource(
            "videoOther",
            "videoOther",
            MediaSource.SOURCE_OTHER,
            MediaSource.TYPE_VIDEO,
        )

        val audioSources = listOf(audioCapture, audioMicrophone, audioOther)
        val videoSources = listOf(videoCamera, videoOther, videoScreen)

        val mappedPermissions = listOf(
            Permission.ContentVideoCamera("videoCamera", "videoCamera"),
            Permission.ContentVideoScreen("videoScreen", "videoScreen"),
            Permission.ContentVideoOther("videoOther", "videoOther"),
            Permission.ContentAudioMicrophone("audioMicrophone", "audioMicrophone"),
            Permission.ContentAudioCapture("audioCapture", "audioCapture"),
            Permission.ContentAudioOther("audioOther", "audioOther"),
        )

        val request = GeckoPermissionRequest.Media(uri, videoSources, audioSources, callback)
        assertEquals(uri, request.uri)
        assertEquals(mappedPermissions.size, request.permissions.size)
        assertTrue(request.permissions.containsAll(mappedPermissions))
    }

    @Test
    fun `grant media permission request`() {
        val callback: GeckoSession.PermissionDelegate.MediaCallback = mock()
        val uri = "https://mozilla.org"

        val audioMicrophone = MockMediaSource(
            "audioMicrophone",
            "audioMicrophone",
            MediaSource.SOURCE_MICROPHONE,
            MediaSource.TYPE_AUDIO,
        )
        val videoCamera = MockMediaSource(
            "videoCamera",
            "videoCamera",
            MediaSource.SOURCE_CAMERA,
            MediaSource.TYPE_VIDEO,
        )

        val audioSources = listOf(audioMicrophone)
        val videoSources = listOf(videoCamera)

        val request = GeckoPermissionRequest.Media(uri, videoSources, audioSources, callback)
        request.grant(request.permissions)
        verify(callback).grant(videoCamera, audioMicrophone)
    }

    @Test
    fun `reject media permission request`() {
        val callback: GeckoSession.PermissionDelegate.MediaCallback = mock()
        val uri = "https://mozilla.org"

        val audioMicrophone = MockMediaSource(
            "audioMicrophone",
            "audioMicrophone",
            MediaSource.SOURCE_MICROPHONE,
            MediaSource.TYPE_AUDIO,
        )
        val videoCamera = MockMediaSource(
            "videoCamera",
            "videoCamera",
            MediaSource.SOURCE_CAMERA,
            MediaSource.TYPE_VIDEO,
        )

        val audioSources = listOf(audioMicrophone)
        val videoSources = listOf(videoCamera)

        val request = GeckoPermissionRequest.Media(uri, videoSources, audioSources, callback)
        request.reject()
        verify(callback).reject()
    }

    class MockMediaSource(id: String, name: String, source: Int, type: Int) : MediaSource() {
        init {
            ReflectionUtils.setField(this, "id", id)
            ReflectionUtils.setField(this, "name", name)
            ReflectionUtils.setField(this, "source", source)
            ReflectionUtils.setField(this, "type", type)
        }
    }

    @Test
    fun `grant all content permission requests`() {
        val uri = "https://mozilla.org"
        val geckoResult1 = mock<GeckoResult<Int>>()
        val geckoResult2 = mock<GeckoResult<Int>>()
        val request1 = GeckoPermissionRequest.Content(uri, PERMISSION_GEOLOCATION, mock(), mutableListOf(geckoResult1))
        val request2 = GeckoPermissionRequest.Content(uri, PERMISSION_GEOLOCATION, mock(), mutableListOf(geckoResult2))
        request1.merge(request2)
        request1.grant()

        verify(geckoResult1).complete(VALUE_ALLOW)
        verify(geckoResult2).complete(VALUE_ALLOW)
        assertTrue(request1.isCompleted)
    }

    @Test
    fun `grant all app permission requests`() {
        val callback1: GeckoSession.PermissionDelegate.Callback = mock()
        val callback2: GeckoSession.PermissionDelegate.Callback = mock()
        val request1 = GeckoPermissionRequest.App(listOf(Manifest.permission.CAMERA), mutableListOf(callback1))
        val request2 = GeckoPermissionRequest.App(listOf(Manifest.permission.CAMERA), mutableListOf(callback2))

        // Both requests are mergeable since permissions are the same.
        request1.merge(request2)
        request1.grant()

        verify(callback1).grant()
        verify(callback2).grant()
    }
}
