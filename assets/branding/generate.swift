import AppKit
import CoreText
let out = CommandLine.arguments[1]
struct Shape { let path: CGPath; let color: String }
func textShapes(_ text: String, size: CGFloat, weight: NSFont.Weight, x: CGFloat, baseline: CGFloat, spacing: CGFloat, color: (Int)->String) -> ([Shape],CGFloat) {
 let font = NSFont.systemFont(ofSize:size,weight:weight)
 let ct = CTFontCreateWithName(font.fontName as CFString,size,nil)
 var result:[Shape]=[]; var cursor=x
 for (i,ch) in text.utf16.enumerated() {
 var c=ch; var glyph:CGGlyph=0; CTFontGetGlyphsForCharacters(ct,&c,&glyph,1)
 var adv=CGSize.zero; CTFontGetAdvancesForGlyphs(ct,.horizontal,&glyph,&adv,1)
 if let p=CTFontCreatePathForGlyph(ct,glyph,nil) {
 var t=CGAffineTransform(a:1,b:0,c:0,d:-1,tx:cursor,ty:baseline)
 result.append(Shape(path:p.copy(using:&t)!,color:color(i)))
 }
 cursor += adv.width+spacing
 }
 return (result,cursor)
}
func pathData(_ p:CGPath)->String {
 var s=""; func n(_ v:CGFloat)->String { String(format:"%.3f",Double(v)) }
 p.applyWithBlock { ptr in
 let e=ptr.pointee; let a=e.points
 switch e.type {
 case .moveToPoint:s += "M\(n(a[0].x)) \(n(a[0].y))"
 case .addLineToPoint:s += "L\(n(a[0].x)) \(n(a[0].y))"
 case .addQuadCurveToPoint:s += "Q\(n(a[0].x)) \(n(a[0].y)) \(n(a[1].x)) \(n(a[1].y))"
 case .addCurveToPoint:s += "C\(n(a[0].x)) \(n(a[0].y)) \(n(a[1].x)) \(n(a[1].y)) \(n(a[2].x)) \(n(a[2].y))"
 case .closeSubpath:s += "Z"
 @unknown default:break
 }
 }; return s
}
func rgb(_ hex:String)->CGColor { let v=UInt32(hex,radix:16)!; return CGColor(red:CGFloat((v>>16)&255)/255,green:CGFloat((v>>8)&255)/255,blue:CGFloat(v&255)/255,alpha:1) }
func save(_ name:String,_ shapes:[Shape],w:Int,h:Int,sizes:[Int]) throws {
 let svg="<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"\(w)\" height=\"\(h)\" viewBox=\"0 0 \(w) \(h)\" role=\"img\" aria-label=\"URL2KeyFill\">"+shapes.map{"<path fill=\"#\($0.color)\" d=\"\(pathData($0.path))\"/>"}.joined()+"</svg>\n"
 try svg.write(toFile:out+"/"+name+".svg",atomically:true,encoding:.utf8)
 for width in sizes {
 let height=Int(Double(width)*Double(h)/Double(w))
 let cs=CGColorSpace(name:CGColorSpace.sRGB)!
 let ctx=CGContext(data:nil,width:width,height:height,bitsPerComponent:8,bytesPerRow:0,space:cs,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
 ctx.translateBy(x:0,y:CGFloat(height));ctx.scaleBy(x:CGFloat(width)/CGFloat(w),y:-CGFloat(height)/CGFloat(h))
 for shape in shapes {ctx.addPath(shape.path);ctx.setFillColor(rgb(shape.color));ctx.fillPath()}
 let rep=NSBitmapImageRep(cgImage:ctx.makeImage()!)
 let suffix=sizes.count>1 ? "-\(width)" : ""
 try rep.representation(using:.png,properties:[:])!.write(to:URL(fileURLWithPath:out+"/"+name+suffix+".png"))
 }
}
for light in [false,true] {
 let main=light ? "252C33":"EEF1F5", gold=light ? "996318":"E4AD54", sub=light ? "546172":"AEB9C6"
 let (word,end)=textShapes("URL2KeyFill",size:126,weight:.semibold,x:24,baseline:132,spacing:-6,color:{ $0 == 3 ? gold:main })
 let (tag,_)=textShapes("WEB GRAPHICS",size:44,weight:.regular,x:28,baseline:209,spacing:5,color:{_ in sub})
 try save(light ? "logo-fundos-claros":"logo-normal",word+tag,w:Int(ceil(end+30)),h:246,sizes:[2400])
}
var icon:[Shape]=[Shape(path:CGPath(roundedRect:CGRect(x:32,y:32,width:960,height:960),cornerWidth:216,cornerHeight:216,transform:nil),color:"252C33")]
// Two video layers, fill and key.
icon.append(Shape(path:CGPath(roundedRect:CGRect(x:200,y:208,width:560,height:360),cornerWidth:48,cornerHeight:48,transform:nil),color:"E4AD54"))
icon.append(Shape(path:CGPath(roundedRect:CGRect(x:264,y:272,width:560,height:360),cornerWidth:48,cornerHeight:48,transform:nil),color:"EEF1F5"))
icon.append(Shape(path:CGPath(roundedRect:CGRect(x:296,y:304,width:496,height:296),cornerWidth:22,cornerHeight:22,transform:nil),color:"252C33"))
let (mark,markWidth)=textShapes("U2KF",size:148,weight:.bold,x:0,baseline:505,spacing:-6,color:{$0 == 1 ? "E4AD54":"EEF1F5"})
var markTransform = CGAffineTransform(translationX:544-markWidth/2,y:0)
icon += mark.map { Shape(path:$0.path.copy(using:&markTransform)!,color:$0.color) }
// Enlarge the video layers and centre them inside the square.
var symbolTransform = CGAffineTransform(a:1.22,b:0,c:0,d:1.22,tx:512-512*1.22,ty:512-420*1.22)
icon = [icon[0]] + icon.dropFirst().map { Shape(path:$0.path.copy(using:&symbolTransform)!,color:$0.color) }
try save("app-icon",icon,w:1024,h:1024,sizes:[16,32,64,128,256,512,1024])
