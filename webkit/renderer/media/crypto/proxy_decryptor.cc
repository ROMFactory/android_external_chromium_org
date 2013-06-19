// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/renderer/media/crypto/proxy_decryptor.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "media/crypto/aes_decryptor.h"
#include "webkit/renderer/media/crypto/key_systems.h"

#if defined(ENABLE_PEPPER_CDMS)
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebMediaPlayerClient.h"
#include "webkit/renderer/media/crypto/ppapi_decryptor.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppapi_webplugin_impl.h"
#endif  // defined(ENABLE_PEPPER_CDMS)

namespace webkit_media {

#if defined(ENABLE_PEPPER_CDMS)
// Returns the PluginInstance associated with the Helper Plugin.
// If a non-NULL pointer is returned, the caller must call closeHelperPlugin()
// when the Helper Plugin is no longer needed.
static scoped_refptr<webkit::ppapi::PluginInstance> CreateHelperPlugin(
    const std::string& plugin_type,
    WebKit::WebMediaPlayerClient* web_media_player_client,
    WebKit::WebFrame* web_frame) {
  DCHECK(web_media_player_client);
  DCHECK(web_frame);

  WebKit::WebPlugin* web_plugin = web_media_player_client->createHelperPlugin(
      WebKit::WebString::fromUTF8(plugin_type), web_frame);
  if (!web_plugin)
    return NULL;

  DCHECK(!web_plugin->isPlaceholder());  // Prevented by Blink.
  // Only Pepper plugins are supported, so it must be a ppapi object.
  webkit::ppapi::WebPluginImpl* ppapi_plugin =
      static_cast<webkit::ppapi::WebPluginImpl*>(web_plugin);
  return ppapi_plugin->instance();
}

void ProxyDecryptor::DestroyHelperPlugin() {
  web_media_player_client_->closeHelperPlugin();
}

#endif  // defined(ENABLE_PEPPER_CDMS)

ProxyDecryptor::ProxyDecryptor(
    WebKit::WebMediaPlayerClient* web_media_player_client,
    WebKit::WebFrame* web_frame,
    const media::KeyAddedCB& key_added_cb,
    const media::KeyErrorCB& key_error_cb,
    const media::KeyMessageCB& key_message_cb,
    const media::NeedKeyCB& need_key_cb)
    : web_media_player_client_(web_media_player_client),
      web_frame_(web_frame),
      key_added_cb_(key_added_cb),
      key_error_cb_(key_error_cb),
      key_message_cb_(key_message_cb),
      need_key_cb_(need_key_cb),
      weak_ptr_factory_(this) {
}

ProxyDecryptor::~ProxyDecryptor() {
  // Destroy the decryptor explicitly before destroying the plugin.
  {
    base::AutoLock auto_lock(lock_);
    decryptor_.reset();
  }
}

// TODO(xhwang): Support multiple decryptor notification request (e.g. from
// video and audio decoders). The current implementation is okay for the current
// media pipeline since we initialize audio and video decoders in sequence.
// But ProxyDecryptor should not depend on media pipeline's implementation
// detail.
void ProxyDecryptor::SetDecryptorReadyCB(
     const media::DecryptorReadyCB& decryptor_ready_cb) {
  base::AutoLock auto_lock(lock_);

  // Cancels the previous decryptor request.
  if (decryptor_ready_cb.is_null()) {
    if (!decryptor_ready_cb_.is_null())
      base::ResetAndReturn(&decryptor_ready_cb_).Run(NULL);
    return;
  }

  // Normal decryptor request.
  DCHECK(decryptor_ready_cb_.is_null());
  if (decryptor_) {
    decryptor_ready_cb.Run(decryptor_.get());
    return;
  }
  decryptor_ready_cb_ = decryptor_ready_cb;
}

bool ProxyDecryptor::InitializeCDM(const std::string& key_system) {
  DVLOG(1) << "InitializeCDM: key_system = " << key_system;

  base::AutoLock auto_lock(lock_);

  DCHECK(!decryptor_);
  decryptor_ = CreateDecryptor(key_system);

  return decryptor_ != NULL;
}


bool ProxyDecryptor::GenerateKeyRequest(const std::string& type,
                                        const uint8* init_data,
                                        int init_data_length) {
  DCHECK(decryptor_);

  if (!decryptor_->GetMediaKeys()->GenerateKeyRequest(
      type, init_data, init_data_length)) {
    decryptor_.reset();
    return false;
  }

  if (!decryptor_ready_cb_.is_null())
    base::ResetAndReturn(&decryptor_ready_cb_).Run(decryptor_.get());

  return true;
}

void ProxyDecryptor::AddKey(const uint8* key,
                            int key_length,
                            const uint8* init_data,
                            int init_data_length,
                            const std::string& session_id) {
  DVLOG(1) << "AddKey()";

  // WebMediaPlayerImpl ensures GenerateKeyRequest() has been called.
  decryptor_->GetMediaKeys()->AddKey(
      key, key_length, init_data, init_data_length, session_id);
}

void ProxyDecryptor::CancelKeyRequest(const std::string& session_id) {
  DVLOG(1) << "CancelKeyRequest()";

  // WebMediaPlayerImpl ensures GenerateKeyRequest() has been called.
  decryptor_->GetMediaKeys()->CancelKeyRequest(session_id);
}

#if defined(ENABLE_PEPPER_CDMS)
scoped_ptr<media::Decryptor> ProxyDecryptor::CreatePpapiDecryptor(
    const std::string& key_system) {
  DCHECK(web_media_player_client_);
  DCHECK(web_frame_);

  std::string plugin_type = GetPepperType(key_system);
  DCHECK(!plugin_type.empty());
  const scoped_refptr<webkit::ppapi::PluginInstance>& plugin_instance =
      CreateHelperPlugin(plugin_type, web_media_player_client_, web_frame_);
  if (!plugin_instance.get()) {
    DVLOG(1) << "ProxyDecryptor: plugin instance creation failed.";
    return scoped_ptr<media::Decryptor>();
  }

  scoped_ptr<webkit_media::PpapiDecryptor> decryptor = PpapiDecryptor::Create(
      key_system,
      plugin_instance,
      base::Bind(&ProxyDecryptor::KeyAdded, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ProxyDecryptor::KeyError, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ProxyDecryptor::KeyMessage, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ProxyDecryptor::NeedKey, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ProxyDecryptor::DestroyHelperPlugin,
                 weak_ptr_factory_.GetWeakPtr()));

  if (!decryptor)
    DestroyHelperPlugin();
  // Else the new object will call destroy_plugin_cb to destroy Helper Plugin.

  return scoped_ptr<media::Decryptor>(decryptor.Pass());
}
#endif  // defined(ENABLE_PEPPER_CDMS)

scoped_ptr<media::Decryptor> ProxyDecryptor::CreateDecryptor(
    const std::string& key_system) {
  if (CanUseAesDecryptor(key_system))
    return scoped_ptr<media::Decryptor>(new media::AesDecryptor(
        base::Bind(&ProxyDecryptor::KeyAdded, weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&ProxyDecryptor::KeyError, weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&ProxyDecryptor::KeyMessage, weak_ptr_factory_.GetWeakPtr()),
        base::Bind(&ProxyDecryptor::NeedKey, weak_ptr_factory_.GetWeakPtr())));

#if defined(ENABLE_PEPPER_CDMS)
  // We only support AesDecryptor and PpapiDecryptor. So if we cannot
  // use the AesDecryptor, then we'll try to create a PpapiDecryptor for given
  // |key_system|.
  return CreatePpapiDecryptor(key_system);
#else
  return scoped_ptr<media::Decryptor>();
#endif  // defined(ENABLE_PEPPER_CDMS)
}

void ProxyDecryptor::KeyAdded(const std::string& session_id) {
  key_added_cb_.Run(session_id);
}

void ProxyDecryptor::KeyError(const std::string& session_id,
                              media::MediaKeys::KeyError error_code,
                              int system_code) {
  key_error_cb_.Run(session_id, error_code, system_code);
}

void ProxyDecryptor::KeyMessage(const std::string& session_id,
                                const std::string& message,
                                const std::string& default_url) {
  key_message_cb_.Run(session_id, message, default_url);
}

void ProxyDecryptor::NeedKey(const std::string& session_id,
                             const std::string& type,
                             scoped_ptr<uint8[]> init_data,
                             int init_data_size) {
  need_key_cb_.Run(session_id, type, init_data.Pass(), init_data_size);
}

}  // namespace webkit_media
