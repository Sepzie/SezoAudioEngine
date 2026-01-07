require 'json'

package = JSON.parse(File.read(File.join(__dir__, '..', 'package.json')))

Pod::Spec.new do |s|
  s.name           = 'ExpoAudioEngine'
  s.version        = package['version']
  s.summary        = package['description'] || 'Sezo Audio Engine Expo Module'
  s.description    = package['description'] || 'Sezo Audio Engine Expo Module'
  s.license        = { :type => 'MIT', :file => '../../../LICENSE' }
  s.authors        = 'Sezo'
  s.homepage       = package['homepage'] || 'https://sepzie.github.io/SezoAudioEngine/'
  s.platforms      = { :ios => '15.1' }
  s.swift_version  = '5.4'
  s.source         = { :git => 'https://github.com/sepzie/SezoAudioEngine.git' }
  s.static_framework = true

  s.dependency 'ExpoModulesCore'

  # Swift/Objective-C compatibility
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES'
  }

  s.source_files = "**/*.{h,m,swift}"
end
